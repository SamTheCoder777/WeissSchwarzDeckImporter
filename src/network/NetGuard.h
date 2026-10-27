#pragma once

// NetGuard: one shared place for
//   - HTTP error classification (429, 5xx, offline, timeout, ...)
//   - per-HOST backoff with Retry-After support (persisted across restarts)
//   - per-KEY minimum check interval + disk cache + ETag/Last-Modified (304)
//   - in-flight de-duplication (same key requested twice = one request)
//
// Main-thread only. Qt 5.15+ (QNetworkRequest::setTransferTimeout).

#include <QByteArray>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <functional>

// ---------------------------------------------------------------- result
struct NetResult
{
    enum Status {
        Ok,           // fresh 2xx from the network
        NotModified,  // 304: cached body, or bodyless if the caller owns the data
        Cached,       // network not used: cache is younger than minIntervalMs
        RateLimited,  // 429, or 403 with X-RateLimit-Remaining: 0
        ServerError,  // 5xx
        Offline,      // DNS / refused / dropped / unknown network error
        Timeout,      // transfer timeout or 408
        ClientError,  // other 4xx (404, 401, ...) - NOT retried, no backoff
        SslError,     // TLS failure - NOT retried, no backoff
        BadBody,      // 2xx but your validate() callback rejected the body
        Cancelled
    };

    Status status = Offline;
    int http = 0;             // HTTP status code, 0 if none
    QHash<QByteArray, QByteArray> headers; // Lower-case names.
    QByteArray header(const QByteArray &name) const { return headers.value(name.toLower()); }
    QByteArray body;          // fresh body, or cached body (see fromCache)
    bool fromCache = false;   // body came from the disk cache
    bool blocked = false;     // true = no request was sent (host is in backoff)
    qint64 retryInMs = 0;     // >0: don't try again before this many ms
    qint64 nextCheckInMs = 0; // for Cached: ms until a real check is allowed
    QString message;          // ready-to-show status text

    bool ok() const { return status == Ok || status == NotModified || status == Cached; }
    bool hasBody() const { return !body.isEmpty(); } // true even on failure if a cache exists
};

// ---------------------------------------------------------------- request
struct NetRequest
{
    enum class Method { Get, Head };
    Method method = Method::Get;
    QUrl url;

    // Accept a bodyless 304 when the caller owns the local representation.
    bool allowNotModified = false;

    // Callbacks run in NetGuard's thread. Use a context in get()/head().
    std::function<void(qint64, qint64)> progress;
    std::function<void()> onStarted; // Once, when the first attempt is sent.

    // Identity of "this thing". Used for cache, interval, de-dupe.
    // Empty = url string. Use e.g. "manifest:EN" if one URL serves several purposes.
    QString key;

    // >0: if the last success for this key is younger than this, return the cached
    // body and do NOT touch the network. (Implies useCache.)
    qint64 minIntervalMs = 0;

    // Save body to disk, send If-None-Match / If-Modified-Since, serve the cache
    // as a fallback body when a request fails.
    bool useCache = false;

    // Skip minIntervalMs and SOFT backoff (offline/timeout/5xx without Retry-After).
    // Never skips HARD backoff (429 / Retry-After): the server told us to wait.
    bool force = false;

    int timeoutMs = 15000;

    // Quiet in-call retries for transient failures (offline/timeout/5xx without
    // Retry-After). Delay = retryDelayMs * attempt. 429 is never retried.
    int retries = 0;

    QByteArray userAgent = "WSDeckImporter/1.0";
    QHash<QByteArray, QByteArray> headers;

    // Optional: return false to reject a 2xx body (e.g. invalid JSON). A rejected
    // body is NOT cached and counts as a failure (soft backoff).
    std::function<bool(const QByteArray &)> validate;
};

// ---------------------------------------------------------------- tuning
struct NetPolicy
{
    // SOFT backoff: offline / timeout / 5xx / bad body. Short, so a user who
    // reconnects is not locked out for long.
    qint64 softBaseMs = 15 * 1000;
    qint64 softMaxMs = 5 * 60 * 1000;

    // HARD backoff: 429 or a supported error response carrying Retry-After.
    qint64 hardBaseMs = 60 * 1000;
    qint64 hardMaxMs = 60 * 60 * 1000;

    // Upper clamp for locally computed backoff only. Server deadlines are preserved.
    qint64 maxServerWaitMs = 24LL * 60 * 60 * 1000;

    double jitter = 0.2;         // +-20% randomisation of computed (not server) waits
    qint64 retryDelayMs = 1000;  // base delay for NetRequest::retries

    // How many requests THIS NetGuard instance runs at once; the rest wait in a
    // FIFO queue. Counts high-level get() requests only (not noteReply downloads).
    int maxConcurrent = 1;
};

// ---------------------------------------------------------------- class
class NetGuard : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const NetResult &)>;

    static NetGuard &instance();
    explicit NetGuard(QObject *parent = nullptr);

    // Call once at startup. Without it everything works in memory only and
    // useCache / minIntervalMs do nothing.
    void setStorageDir(const QString &dir);

    NetPolicy &policy() { return policy_; }
    QNetworkAccessManager *nam() { return &nam_; }

    // ---- high level: small GET requests (manifests, JSON, API calls) ----
    // The callback is ALWAYS called asynchronously, exactly once, and not at
    // all if `context` was destroyed meanwhile. Returns an id for cancel().
    quint64 get(const NetRequest &req, QObject *context, Callback cb);
    quint64 head(const NetRequest &req, QObject *context, Callback cb);
    void cancel(quint64 id);               // cancel one waiter
    void cancelKey(const QString &key);    // cancel the request + all its waiters
    bool isBusy(const QString &key) const { return inflight_.contains(key); }

    // ---- low level: big/streamed downloads that you run yourself ----
    // Ask before sending: returns ms to wait (0 = go ahead).
    qint64 blockedForMs(const QUrl &url, bool force = false) const;
    // Report after finishing: updates the host backoff state, returns the verdict.
    // Call it while the reply is still valid (before deleteLater/nullptr).
    NetResult noteReply(const QUrl &requestedUrl, QNetworkReply *reply);

    // Forget backoff (one host, or all if empty). E.g. a "retry now" debug action.
    void clearBackoff(const QString &host = QString());

    static QString minsText(qint64 ms);

private:
    struct HostState {
        int fails = 0;
        qint64 retryNotBefore = 0;
        bool hard = false;
        int lastStatus = NetResult::Offline;
    };
    struct KeyState {
        qint64 lastOk = 0;
        QString etag;
        QString lastModified;
    };
    struct Waiter {
        quint64 id = 0;
        QPointer<QObject> ctx;
        bool hasCtx = false;
        Callback cb;
        std::function<void(qint64, qint64)> progress;
        std::function<void()> onStarted;
    };
    struct Inflight {
        NetRequest req;
        QPointer<QNetworkReply> reply;
        QVector<Waiter> waiters;
        quint64 serial = 0;
        int attempt = 0;
        bool notifiedStarted = false;
        bool recovered304 = false;
        bool started = false; // false = still waiting in the queue
    };

    void pump();                       // start queued requests while slots are free
    void send(const QString &key);
    NetResult blockedResult(const NetRequest &req) const;
    void finish(const QString &key, const NetResult &result);
    void onFinished(const QString &key, QNetworkReply *reply, quint64 serial);
    NetResult applyOutcome(const QUrl &url, QNetworkReply *reply, NetResult::Status st,
                           int http, qint64 serverWait, qint64 now);

    void deliver(const QVector<Waiter> &ws, const NetResult &r);
    void deliverLater(const QVector<Waiter> &ws, const NetResult &r);

    static NetResult::Status classify(QNetworkReply *reply, int http);
    static qint64 retryAfterMs(QNetworkReply *reply, qint64 now);
    static qint64 serverWaitFor(QNetworkReply *reply, NetResult::Status st, qint64 now);
    static QString describe(NetResult::Status st, int http, const QString &err);

    QString statePath() const { return dir_ + "/netguard_state.json"; }
    QString cachePath(const QString &key) const;
    bool hasCache(const QString &key) const;
    QByteArray readCache(const QString &key) const;
    bool writeCache(const QString &key, const QByteArray &body);
    void loadState();
    void saveState() const;

    QNetworkAccessManager nam_;
    NetPolicy policy_;
    QString dir_;
    QHash<QString, HostState> hosts_;   // backoff, keyed by host
    QHash<QString, KeyState> keys_;     // last success + validators, keyed by NetRequest::key
    QHash<QString, Inflight> inflight_; // running requests, keyed by NetRequest::key
    quint64 nextId_ = 0;
    quint64 nextSerial_ = 0;
    QStringList pending_;               // keys waiting for a free slot (FIFO)
    QHash<quint64, Waiter> readyWaiters_;
    QHash<quint64, NetResult> readyResults_;
    int running_ = 0;                   // slots in use
};
