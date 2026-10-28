#include "NetGuard.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimer>
#include <limits>
#include <utility>

namespace {
qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
}

// =====================================================================
//  setup / helpers
// =====================================================================

NetGuard &NetGuard::instance()
{
    // parented to qApp so it is destroyed before the event loop machinery
    static NetGuard *g = new NetGuard(qApp);
    return *g;
}

NetGuard::NetGuard(QObject *parent)
    : QObject(parent)
{}

void NetGuard::setStorageDir(const QString &dir)
{
    dir_ = dir;
    QDir().mkpath(dir_ + "/cache");
    loadState();
}

QString NetGuard::minsText(qint64 ms)
{
    ms = qMax<qint64>(0, ms);
    if (ms < 60000)
        return QString("%1 s").arg(qMax<qint64>(1, ms / 1000 + (ms % 1000 != 0)));
    const qint64 m = ms / 60000 + (ms % 60000 != 0);
    if (m < 120)
        return QString("%1 min").arg(m);
    return QString("%1 h").arg((m + 59) / 60);
}

QString NetGuard::describe(NetResult::Status st, int http, const QString &err)
{
    const QString code = http > 0 ? QString(" (HTTP %1)").arg(http) : QString();
    switch (st) {
    case NetResult::Ok:          return QStringLiteral("OK");
    case NetResult::NotModified: return QStringLiteral("Up to date");
    case NetResult::Cached:      return QStringLiteral("Using cached data");
    case NetResult::RateLimited: return QString("Rate limited by server") + code;
    case NetResult::ServerError: return QString("Server error") + code;
    case NetResult::Offline:
        return err.isEmpty() ? QString("Can't reach server") : QString("Can't reach server: ") + err;
    case NetResult::Timeout:     return QStringLiteral("Request timed out");
    case NetResult::ClientError: return QString("Server refused the request") + code;
    case NetResult::SslError:
        return err.isEmpty() ? QString("Secure connection failed")
                             : QString("Secure connection failed: ") + err;
    case NetResult::BadBody:     return QStringLiteral("Server sent an invalid response");
    case NetResult::Cancelled:   return QStringLiteral("Cancelled");
    }
    return {};
}

// =====================================================================
//  classification
// =====================================================================

// Turns "whatever Qt + HTTP gave us" into one Status. Check the HTTP code
// BEFORE the Qt error: Qt reports 429 as a generic content error.
NetResult::Status NetGuard::classify(QNetworkReply *reply, int http)
{
    const auto error = reply->error();
    if (error == QNetworkReply::OperationCanceledError)
        return NetResult::Cancelled;
    if (error == QNetworkReply::TimeoutError)
        return NetResult::Timeout;
    if (http == 429 || (http == 403
        && reply->rawHeader("X-RateLimit-Remaining").trimmed() == "0"))
        return NetResult::RateLimited;
    if (http == 408) return NetResult::Timeout;
    if (http >= 500 && http < 600) return NetResult::ServerError;
    if (http >= 400 && http < 500) return NetResult::ClientError;
    if (error == QNetworkReply::SslHandshakeFailedError)
        return NetResult::SslError;
    if (error != QNetworkReply::NoError) return NetResult::Offline;
    if (http == 304) return NetResult::NotModified;
    if (http >= 200 && http < 300) return NetResult::Ok;
    return NetResult::BadBody; // Unhandled redirects / unexpected status.
}

// "How long did the server tell us to wait?" (0 = it didn't)
qint64 NetGuard::retryAfterMs(QNetworkReply *reply, qint64 now)
{
    const qint64 maximum = std::numeric_limits<qint64>::max() - now;
    const QByteArray value = reply->rawHeader("Retry-After").trimmed();
    if (!value.isEmpty()) {
        bool valid = false;
        const qulonglong seconds = value.toULongLong(&valid);
        if (valid) {
            if (seconds > qulonglong(maximum / 1000)) return maximum;
            return qint64(seconds) * 1000;
        }
        const QDateTime date = QDateTime::fromString(
            QString::fromLatin1(value), Qt::RFC2822Date);
        if (date.isValid())
            return qMax<qint64>(0, date.toMSecsSinceEpoch() - now);
    }
    if (reply->rawHeader("X-RateLimit-Remaining").trimmed() == "0") {
        bool valid = false;
        const qulonglong seconds = reply->rawHeader("X-RateLimit-Reset")
                                      .trimmed().toULongLong(&valid);
        if (valid) {
            if (seconds > qulonglong(std::numeric_limits<qint64>::max() / 1000))
                return maximum;
            return qMax<qint64>(0, qint64(seconds) * 1000 - now);
        }
    }
    return 0;
}

qint64 NetGuard::serverWaitFor(QNetworkReply *reply, NetResult::Status status, qint64 now)
{
    return (status == NetResult::RateLimited || status == NetResult::ServerError
            || status == NetResult::ClientError || status == NetResult::Timeout)
        ? retryAfterMs(reply, now) : 0;
}

// =====================================================================
//  backoff state machine
// =====================================================================

// Updates per-host state from one finished request and builds the verdict.
// Shared by get() (high level) and noteReply() (low level / streaming).
NetResult NetGuard::applyOutcome(const QUrl &url, QNetworkReply *reply,
    NetResult::Status status, int http, qint64 serverWait, qint64 now)
{
    NetResult result;
    result.status = status;
    result.http = http;
    if (reply) {
        for (const auto &pair : reply->rawHeaderPairs())
            result.headers.insert(pair.first.toLower(), pair.second);
    }
    const QString host = url.host();
    const auto previous = hosts_.value(host);
    const bool hardActive = previous.hard && previous.retryNotBefore > now;
    const bool failed = status == NetResult::RateLimited
        || status == NetResult::ServerError || status == NetResult::Offline
        || status == NetResult::Timeout || status == NetResult::BadBody
        || serverWait > 0;

    if (failed) {
        HostState next = previous;
        next.fails = qMin(qMax(0, previous.fails), 1000000) + 1;
        const bool newHard = status == NetResult::RateLimited || serverWait > 0;
        const qint64 base = newHard ? policy_.hardBaseMs : policy_.softBaseMs;
        const qint64 cap = newHard ? policy_.hardMaxMs : policy_.softMaxMs;
        qint64 calculated = qMin(base * (1LL << qMin(next.fails - 1, 6)), cap);
        const double jitter = 1.0 + policy_.jitter
            * (2.0 * QRandomGenerator::global()->generateDouble() - 1.0);
        calculated = qBound(qint64{0}, qint64(double(calculated) * jitter),
                            policy_.maxServerWaitMs);
        const qint64 wait = qMax(calculated, serverWait);
        next.retryNotBefore = now + wait;
        next.hard = newHard;
        next.lastStatus = int(status);
        if (hardActive) {
            next.retryNotBefore = qMax(next.retryNotBefore, previous.retryNotBefore);
            next.hard = true;
            if (!newHard) next.lastStatus = previous.lastStatus;
        }
        hosts_.insert(host, next);
        result.retryInMs = qMax<qint64>(0, next.retryNotBefore - now);
        saveState();
    } else if ((status == NetResult::Ok || status == NetResult::NotModified
                || status == NetResult::ClientError) && !hardActive) {
        if (hosts_.remove(host)) saveState();
    }
    result.message = describe(status, http, reply ? reply->errorString() : QString());
    if (result.retryInMs > 0)
        result.message += " · retry in " + minsText(result.retryInMs);
    return result;
}

// 0 = free to send. >0 = ms until this host may be contacted again.
qint64 NetGuard::blockedForMs(const QUrl &url, bool force) const
{
    const auto it = hosts_.constFind(url.host());
    if (it == hosts_.constEnd())
        return 0;
    const qint64 remain = it->retryNotBefore - nowMs();
    if (remain <= 0)
        return 0;
    if (force && !it->hard)  // "retry now" may override our own guess, not the server's order
        return 0;
    return remain;
}

NetResult NetGuard::noteReply(const QUrl &requestedUrl, QNetworkReply *reply)
{
    const qint64 now = nowMs();
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const NetResult::Status st = classify(reply, http);
    return applyOutcome(requestedUrl, reply, st, http, serverWaitFor(reply, st, now), now);
}

void NetGuard::clearBackoff(const QString &host)
{
    if (host.isEmpty())
        hosts_.clear();
    else
        hosts_.remove(host);
    saveState();
}

// =====================================================================
//  public get(): the four gates
// =====================================================================

quint64 NetGuard::get(const NetRequest &input, QObject *context, Callback callback)
{
    NetRequest request = input;
    if (request.key.isEmpty()) request.key = request.url.toString(QUrl::FullyEncoded);
    if (request.method == NetRequest::Method::Head) {
        request.useCache = false;
        request.minIntervalMs = 0;
        request.allowNotModified = true;
    } else if (request.minIntervalMs > 0) {
        request.useCache = true;
    }
    Waiter waiter;
    waiter.id = ++nextId_;
    waiter.ctx = context;
    waiter.hasCtx = context != nullptr;
    waiter.cb = std::move(callback);
    waiter.progress = request.progress;
    waiter.onStarted = request.onStarted;

    auto existing = inflight_.find(request.key);
    if (existing != inflight_.end()) {
        // Keys identify one method, URL and representation. Never silently merge
        // mismatched requests if a caller accidentally reuses a key.
        if (existing->req.url != request.url || existing->req.method != request.method
            || existing->req.headers != request.headers) {
            NetResult bad;
            bad.status = NetResult::BadBody;
            bad.message = "Request key reused for a different request";
            deliverLater({waiter}, bad);
        } else {
            existing->waiters.append(waiter);
            if (existing->notifiedStarted && waiter.onStarted) {
                QTimer::singleShot(0, this, [this, key = request.key,
                    serial = existing->serial, id = waiter.id] {
                    const auto flight = inflight_.constFind(key);
                    if (flight == inflight_.constEnd() || flight->serial != serial) return;
                    for (const auto &current : flight->waiters) {
                        if (current.id == id) {
                            const auto copy = current;
                            if ((!copy.hasCtx || copy.ctx) && copy.onStarted)
                                copy.onStarted();
                            return;
                        }
                    }
                });
            }
        }
        return waiter.id;
    }
    if (!request.url.isValid() || request.url.host().isEmpty()
        || (request.url.scheme() != "https" && request.url.scheme() != "http")) {
        NetResult bad;
        bad.status = NetResult::BadBody;
        bad.message = "Invalid HTTP(S) URL";
        deliverLater({waiter}, bad);
        return waiter.id;
    }
    const qint64 now = nowMs();
    const KeyState state = keys_.value(request.key);
    if (!request.force && request.minIntervalMs > 0 && state.lastOk > 0
        && now >= state.lastOk && now - state.lastOk < request.minIntervalMs) {
        QFile cache(cachePath(request.key));
        if (!dir_.isEmpty() && cache.open(QIODevice::ReadOnly)) {
            const QByteArray body = cache.readAll();
            if (!request.validate || request.validate(body)) {
                NetResult result;
                result.status = NetResult::Cached;
                result.body = body;
                result.fromCache = true;
                result.nextCheckInMs = request.minIntervalMs - (now - state.lastOk);
                result.message = "Using cached data";
                deliverLater({waiter}, result);
                return waiter.id;
            }
        }
    }
    if (blockedForMs(request.url, request.force) > 0) {
        deliverLater({waiter}, blockedResult(request));
        return waiter.id;
    }
    Inflight flight;
    flight.req = request;
    flight.serial = ++nextSerial_;
    flight.waiters.append(waiter);
    inflight_.insert(request.key, flight);
    pending_.append(request.key);
    // Start asynchronously: callers can store the returned ID first.
    QTimer::singleShot(0, this, &NetGuard::pump);
    return waiter.id;
}

quint64 NetGuard::head(const NetRequest &input, QObject *context, Callback callback)
{
    NetRequest request = input;
    request.method = NetRequest::Method::Head;
    request.key = "HEAD:" + (input.key.isEmpty()
        ? input.url.toString(QUrl::FullyEncoded) : input.key);
    return get(request, context, std::move(callback));
}

NetResult NetGuard::blockedResult(const NetRequest &request) const
{
    NetResult result;
    result.blocked = true;
    result.status = NetResult::Status(hosts_.value(request.url.host()).lastStatus);
    result.retryInMs = blockedForMs(request.url, request.force);
    if (request.useCache && hasCache(request.key)) {
        result.body = readCache(request.key);
        result.fromCache = true;
    }
    result.message = describe(result.status, 0, QString())
        + " · retry in " + minsText(result.retryInMs);
    return result;
}

void NetGuard::finish(const QString &key, const NetResult &result)
{
    auto it = inflight_.find(key);
    if (it == inflight_.end()) return;
    const auto waiters = it->waiters;
    if (it->started) --running_;
    inflight_.erase(it);
    deliverLater(waiters, result);
    QTimer::singleShot(0, this, &NetGuard::pump);
}

// =====================================================================
//  sending + finishing
// =====================================================================

// Starts queued requests while there are free slots.
// Called when a request is queued and whenever a slot is released.
void NetGuard::pump()
{
    while (running_ < qMax(1, policy_.maxConcurrent) && !pending_.isEmpty()) {
        const QString key = pending_.takeFirst();
        auto it = inflight_.find(key);
        if (it == inflight_.end() || it->started) continue;
        if (blockedForMs(it->req.url, it->req.force) > 0) {
            const auto result = blockedResult(it->req);
            finish(key, result);
            continue;
        }
        it->started = true;
        ++running_;
        send(key);
    }
}

void NetGuard::send(const QString &key)
{
    auto it = inflight_.find(key);
    if (it == inflight_.end()) return;
    const NetRequest request = it->req;
    const quint64 serial = it->serial;
    // Every attempt, including delayed retries, obeys current host cooldowns.
    if (blockedForMs(request.url, request.force) > 0) {
        finish(key, blockedResult(request));
        return;
    }
    if (!it->notifiedStarted) {
        it->notifiedStarted = true;
        const auto waiters = it->waiters;
        for (const auto &waiter : waiters) {
            if ((!waiter.hasCtx || waiter.ctx) && waiter.onStarted)
                waiter.onStarted();
        }
    }
    it = inflight_.find(key);
    if (it == inflight_.end() || it->serial != serial) return;
    QNetworkRequest networkRequest(request.url);
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                QNetworkRequest::NoLessSafeRedirectPolicy);
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader, request.userAgent);
    networkRequest.setTransferTimeout(request.timeoutMs);
    for (auto h = request.headers.constBegin(); h != request.headers.constEnd(); ++h)
        networkRequest.setRawHeader(h.key(), h.value());
    if (request.useCache && hasCache(key)) {
        const KeyState state = keys_.value(key);
        if (!state.etag.isEmpty())
            networkRequest.setRawHeader("If-None-Match", state.etag.toLatin1());
        if (!state.lastModified.isEmpty())
            networkRequest.setRawHeader("If-Modified-Since", state.lastModified.toLatin1());
    }
    QNetworkReply *reply = request.method == NetRequest::Method::Head
        ? nam_.head(networkRequest) : nam_.get(networkRequest);
    it->reply = reply;
    if (request.maxBodyBytes > 0) {
        reply->setReadBufferSize(request.maxBodyBytes + 1);
        connect(reply, &QIODevice::readyRead, this, [reply, limit = request.maxBodyBytes] {
            if (reply->bytesAvailable() > limit) {
                reply->setProperty("netguardBodyTooLarge", true);
                reply->abort();
            }
        });
    }
    connect(reply, &QNetworkReply::downloadProgress, this,
        [this, key, serial](qint64 received, qint64 total) {
            const auto current = inflight_.constFind(key);
            if (current == inflight_.constEnd() || current->serial != serial) return;
            const auto waiters = current->waiters;
            for (const auto &waiter : waiters) {
                if ((!waiter.hasCtx || waiter.ctx) && waiter.progress)
                    waiter.progress(received, total);
            }
        });
    connect(reply, &QNetworkReply::finished, this,
        [this, key, reply, serial] { onFinished(key, reply, serial); });
}

void NetGuard::onFinished(const QString &key, QNetworkReply *reply, quint64 serial)
{
    reply->deleteLater();
    auto it = inflight_.find(key);
    if (it == inflight_.end() || it->serial != serial || it->reply != reply) return;
    const NetRequest request = it->req;
    const qint64 now = nowMs();
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto status = classify(reply, http);
    if (reply->property("netguardBodyTooLarge").toBool()) status = NetResult::BadBody;
    const qint64 serverWait = serverWaitFor(reply, status, now);
    const QByteArray body = reply->readAll();
    QByteArray cachedBody;
    bool readableCache = false;
    if (request.useCache && !dir_.isEmpty()) {
        QFile cache(cachePath(key));
        if (cache.open(QIODevice::ReadOnly)) {
            cachedBody = cache.readAll();
            readableCache = !request.validate || request.validate(cachedBody);
        }
    }
    if (status == NetResult::NotModified
        && request.method == NetRequest::Method::Get
        && !request.allowNotModified && !readableCache) {
        // Recover at most once, and remove caller-provided validators too.
        if (!it->recovered304) {
            it->recovered304 = true;
            keys_.remove(key);
            if (!dir_.isEmpty()) QFile::remove(cachePath(key));
            saveState();
            for (auto h = it->req.headers.begin(); h != it->req.headers.end();) {
                if (h.key().toLower() == "if-none-match"
                    || h.key().toLower() == "if-modified-since")
                    h = it->req.headers.erase(h);
                else ++h;
            }
            it->reply.clear();
            send(key);
            return;
        }
        status = NetResult::BadBody;
    }
    if (status == NetResult::Ok && request.method == NetRequest::Method::Get
        && request.validate && !request.validate(body))
        status = NetResult::BadBody;

    const bool transient = status == NetResult::Offline || status == NetResult::Timeout
        || (status == NetResult::ServerError && serverWait == 0);
    if (transient && serverWait == 0 && it->attempt < request.retries) {
        ++it->attempt;
        it->reply.clear();
        const qint64 delay = qBound(qint64{0}, policy_.retryDelayMs * qint64(it->attempt),
                                   qint64(std::numeric_limits<int>::max()));
        QTimer::singleShot(int(delay), this, [this, key, serial] {
            const auto current = inflight_.constFind(key);
            if (current != inflight_.constEnd() && current->serial == serial)
                send(key);
        });
        return;
    }
    // Use the requested host for admission/backoff consistently across redirects.
    NetResult result = applyOutcome(request.url, reply, status, http, serverWait, now);
    if (status == NetResult::Ok) {
        result.body = body;
        if (request.useCache && writeCache(key, body)) {
            KeyState &state = keys_[key];
            state.lastOk = now;
            state.etag = QString::fromLatin1(reply->rawHeader("ETag"));
            state.lastModified = QString::fromLatin1(reply->rawHeader("Last-Modified"));
            saveState();
        }
    } else if (status == NetResult::NotModified && readableCache) {
        result.body = cachedBody;
        result.fromCache = true;
        keys_[key].lastOk = now;
        const auto etag = reply->rawHeader("ETag");
        const auto modified = reply->rawHeader("Last-Modified");
        if (!etag.isEmpty()) keys_[key].etag = QString::fromLatin1(etag);
        if (!modified.isEmpty()) keys_[key].lastModified = QString::fromLatin1(modified);
        saveState();
    } else if (request.useCache && readableCache) {
        result.body = cachedBody;
        result.fromCache = true;
    }
    finish(key, result);
}

// =====================================================================
//  callbacks / cancel
// =====================================================================

void NetGuard::deliver(const QVector<Waiter> &ws, const NetResult &r)
{
    for (const Waiter &w : ws) {
        if (w.hasCtx && w.ctx.isNull())
            continue; // owner died, don't call into a dead object
        if (w.cb)
            w.cb(r);
    }
}

void NetGuard::deliverLater(const QVector<Waiter> &waiters, const NetResult &result)
{
    for (const auto &waiter : waiters) {
        const bool alreadyQueued = readyWaiters_.contains(waiter.id);
        readyWaiters_.insert(waiter.id, waiter);
        readyResults_.insert(waiter.id, result);
        if (alreadyQueued) continue;
        QTimer::singleShot(0, this, [this, id = waiter.id] {
            const auto found = readyWaiters_.find(id);
            if (found == readyWaiters_.end()) return;
            const Waiter waiter = found.value();
            const NetResult result = readyResults_.take(id);
            readyWaiters_.erase(found);
            deliver({waiter}, result);
        });
    }
}

void NetGuard::cancelKey(const QString &key)
{
    auto it = inflight_.find(key);
    if (it == inflight_.end())
        return;
    const QVector<Waiter> ws = it->waiters;
    QPointer<QNetworkReply> rep = it->reply;
    if (it->started)
        --running_;
    inflight_.erase(it); // erase first: onFinished will see nothing and ignore the abort

    NetResult r;
    r.status = NetResult::Cancelled;
    r.message = describe(r.status, 0, QString());
    deliverLater(ws, r);
    if (rep)
        rep->abort();
    QTimer::singleShot(0, this, &NetGuard::pump);
}

void NetGuard::cancel(quint64 id)
{
    if (readyWaiters_.contains(id)) {
        NetResult result;
        result.status = NetResult::Cancelled;
        result.message = "Cancelled";
        readyResults_.insert(id, result);
        return;
    }
    for (auto it = inflight_.begin(); it != inflight_.end(); ++it) {
        for (int i = 0; i < it->waiters.size(); ++i) {
            if (it->waiters[i].id != id)
                continue;
            const Waiter w = it->waiters.takeAt(i);
            NetResult r;
            r.status = NetResult::Cancelled;
            r.message = describe(r.status, 0, QString());
            deliverLater(QVector<Waiter>{w}, r);

            if (it->waiters.isEmpty()) { // nobody left interested: stop the transfer
                QPointer<QNetworkReply> rep = it->reply;
                if (it->started)
                    --running_;
                inflight_.erase(it);
                if (rep)
                    rep->abort();
                QTimer::singleShot(0, this, &NetGuard::pump);
            }
            return;
        }
    }
}

// =====================================================================
//  disk: cache + state
// =====================================================================

QString NetGuard::cachePath(const QString &key) const
{
    const QByteArray h = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return dir_ + "/cache/" + QString::fromLatin1(h) + ".body";
}

bool NetGuard::hasCache(const QString &key) const
{
    return !dir_.isEmpty() && QFile::exists(cachePath(key));
}

QByteArray NetGuard::readCache(const QString &key) const
{
    if (dir_.isEmpty())
        return {};
    QFile f(cachePath(key));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool NetGuard::writeCache(const QString &key, const QByteArray &body)
{
    if (dir_.isEmpty()) return false;
    QSaveFile file(cachePath(key));
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(body) != body.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

void NetGuard::loadState()
{
    hosts_.clear();
    keys_.clear();
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

    const QJsonObject h = root.value("hosts").toObject();
    for (auto it = h.begin(); it != h.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        HostState s;
        s.fails = o.value("fails").toInt();
        s.retryNotBefore = qint64(o.value("retryNotBefore").toDouble());
        s.hard = o.value("hard").toBool();
        s.lastStatus = o.value("lastStatus").toInt(int(NetResult::Offline));
        hosts_.insert(it.key(), s);
    }
    const QJsonObject k = root.value("keys").toObject();
    for (auto it = k.begin(); it != k.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        KeyState s;
        s.lastOk = qint64(o.value("lastOk").toDouble());
        s.etag = o.value("etag").toString();
        s.lastModified = o.value("lastModified").toString();
        keys_.insert(it.key(), s);
    }
}

void NetGuard::saveState() const
{
    if (dir_.isEmpty())
        return;
    QJsonObject h, k;
    for (auto it = hosts_.constBegin(); it != hosts_.constEnd(); ++it)
        h.insert(it.key(),
                 QJsonObject{{"fails", it->fails},
                             {"retryNotBefore", double(it->retryNotBefore)},
                             {"hard", it->hard},
                             {"lastStatus", it->lastStatus}});
    for (auto it = keys_.constBegin(); it != keys_.constEnd(); ++it)
        k.insert(it.key(),
                 QJsonObject{{"lastOk", double(it->lastOk)},
                             {"etag", it->etag},
                             {"lastModified", it->lastModified}});

    QSaveFile f(statePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{"hosts", h}, {"keys", k}}).toJson(QJsonDocument::Compact));
        f.commit();
    }
}
