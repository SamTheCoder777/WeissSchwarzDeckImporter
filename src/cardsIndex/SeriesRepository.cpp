#include "SeriesRepository.h"
#include "../core/Config.h"

#include <QFile>
#include <QFileInfo>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>

static QString normalizeEtag(QString e)
{
    e = e.trimmed();
    if (e.startsWith("W/"))
        e.remove(0, 2);
    e.remove('"');
    for (const char *suffix : {"-gzip", "--gzip", "-br"})
        if (e.endsWith(suffix))
            e.chop(int(strlen(suffix)));
    return e;
}

SeriesRepository::SeriesRepository(QObject *parent)
    : QObject(parent)
{
    ensureSeriesSchema(Region::JP);
    ensureSeriesSchema(Region::EN);
    ensureCardSchema();

    connect(this, &SeriesRepository::cardListDownloaded, this, [this](const QString &id) {
        outdated_.removeAll(id);
        emit cardListUpdateState(id, false);
        if (updatingAll_)
            QTimer::singleShot(0, this, &SeriesRepository::pumpUpdateQueue);
    });
    connect(this, &SeriesRepository::errorOccurred, this, [this](const QString &) {
        if (updatingAll_)
            QTimer::singleShot(0, this, &SeriesRepository::pumpUpdateQueue);
    });
    connect(&Config::instance(), &Config::detectLocaleModeChanged, this, [this] {
        ensureCardSchema();
        setStatus("Ready.");
        emit seriesListUpdated();
        checkForUpdates();

        ++cardCheckGen_;
        cardCheckQueue_.clear();
        cardCheckActive_ = 0;
        outdated_.clear();
        updateQueue_.clear();
        updatingAll_ = false;
        checkCardListUpdates();
    });
}

QSqlDatabase SeriesRepository::seriesDb(Region region) const
{
    const QString name = "series_catalog_connection_" + regionTag(region);
    const QString path = Config::instance().getSeriesListDatabasePath(region);

    QSqlDatabase db = QSqlDatabase::contains(name) ? QSqlDatabase::database(name, false)
                                                   : QSqlDatabase::addDatabase("QSQLITE", name);
    if (db.databaseName() != path)
        db.setDatabaseName(path);
    if (!db.isOpen() && !db.open())
        qCritical() << path << "open failed:" << db.lastError().text();
    return db;
}

bool SeriesRepository::hasLocalSeries(Region region) const
{
    QSqlQuery q(seriesDb(region));
    return q.exec("SELECT COUNT(*) FROM series") && q.next() && q.value(0).toInt() > 0;
}

QSqlDatabase SeriesRepository::cardDb(Region region) const
{
    const QString name = "cardlist_catalog_connection_" + regionTag(region);
    const QString path = Config::instance().getCardListDatabasePath(region);

    QSqlDatabase db = QSqlDatabase::contains(name) ? QSqlDatabase::database(name, false)
                                                   : QSqlDatabase::addDatabase("QSQLITE", name);
    if (db.databaseName() != path)
        db.setDatabaseName(path);
    if (!db.isOpen() && !db.open())
        qCritical() << path << "open failed:" << db.lastError().text();
    return db;
}

void SeriesRepository::ensureSeriesSchema(Region region)
{
    QSqlQuery q(seriesDb(region));
    q.exec("CREATE TABLE IF NOT EXISTS series ("
           "  id TEXT PRIMARY KEY,"
           "  set_code TEXT,"
           "  name TEXT,"
           "  hash TEXT)");
}

void SeriesRepository::ensureCardSchema(Region region)
{
    QSqlQuery q(cardDb(region));
    q.exec("CREATE TABLE IF NOT EXISTS cards ("
           "  series_id TEXT,"
           "  card_id   TEXT,"
           "  cardcode  TEXT,"
           "  data      TEXT,"
           "  PRIMARY KEY (series_id, card_id))");
    q.exec("CREATE INDEX IF NOT EXISTS idx_cards_cardcode ON cards(cardcode)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_cards_series ON cards(series_id)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_cards_cardcode_lower ON cards(LOWER(cardcode))");
}

QVector<SeriesRepository::SeriesRow> SeriesRepository::loadSeries() {
    QVector<SeriesRow> loaded;
    QSqlQuery q(seriesDb());
    if (q.exec("SELECT id, set_code, name, hash FROM series ORDER BY name")) {
        while (q.next()) {
            SeriesRow r;
            r.id   = q.value(0).toString();
            r.set  = q.value(1).toString();
            r.name = q.value(2).toString();
            r.hash = q.value(3).toString();
            loaded.push_back(r);
        }
    }

    QSet<QString> have;
    QSqlQuery c(cardDb());
    if (c.exec("SELECT DISTINCT series_id FROM cards"))
        while (c.next()) have.insert(c.value(0).toString());
    for (SeriesRow& r : loaded) r.hasCardList = have.contains(r.id);

    return loaded;
}

void SeriesRepository::saveSeriesToDb(Region region, const QVector<SeriesRow> &rows)
{
    QSqlDatabase db = seriesDb(region);
    db.transaction();
    QSqlQuery q(db);
    q.exec("DELETE FROM series");
    q.prepare("INSERT INTO series (id, set_code, name, hash) VALUES (?,?,?,?)");
    for (const SeriesRow& r : rows) {
        q.addBindValue(r.id);
        q.addBindValue(r.set);
        q.addBindValue(r.name);
        q.addBindValue(r.hash);
        if (!q.exec()) qWarning() << "series insert failed:" << q.lastError().text();
    }
    db.commit();
}

void SeriesRepository::checkForUpdates(Region region)
{
    const QString tag = regionTag(region);
    setStatus(tag + ": checking for series list updates…");

    QNetworkRequest req{QUrl(Config::instance().getSeriesListUrl(region))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "WSDeckImporter/1.0");

    QNetworkReply *head = nam_.head(req);
    connect(head, &QNetworkReply::finished, this, [this, head, region, tag] {
        head->deleteLater();
        const bool haveLocal = hasLocalSeries(region);

        if (head->error() != QNetworkReply::NoError) {
            if (haveLocal) {
                setStatus(tag + ": server unreachable. Using local series list.");
                emit updateAvailable(region, UpdateStatus::UpToDate);
            } else {
                setStatus(tag + ": cannot reach server and no local series list.");
                emit updateAvailable(region, UpdateStatus::Error);
            }
            return;
        }

        const QString remoteEtag = QString::fromUtf8(head->rawHeader("ETag"));
        const QString cachedEtag = Config::instance().getSeriesListEtag(region);

        if (!haveLocal) {
            setStatus(tag + ": no local series list. Download needed.");
            emit updateAvailable(region, UpdateStatus::Error);
        } else if (!remoteEtag.isEmpty() && remoteEtag == cachedEtag) {
            setStatus(tag + ": series list is up to date.");
            emit updateAvailable(region, UpdateStatus::UpToDate);
        } else {
            setStatus(tag + ": series list update available.");
            emit updateAvailable(region, UpdateStatus::UpdateAvailable);
        }
    });
}

void SeriesRepository::refreshSeriesList(Region region)
{
    if (reply_)
        return;
    const QString tag = regionTag(region);
    setStatus(tag + ": checking series list…");

    QNetworkRequest req{QUrl(Config::instance().getSeriesListUrl(region))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    const QString etag = Config::instance().getSeriesListEtag(region);
    if (!etag.isEmpty())
        req.setRawHeader("If-None-Match", etag.toUtf8());

    reply_ = nam_.get(req);
    emit busyChanged();

    connect(reply_,
            &QNetworkReply::downloadProgress,
            this,
            [this, region](qint64 got, qint64 total) { emit seriesProgress(region, got, total); });

    connect(reply_, &QNetworkReply::finished, this, [this, region, tag] {
        const int http = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray newEtag = reply_->rawHeader("ETag");
        const bool netOk = (reply_->error() == QNetworkReply::NoError);
        const QString err = reply_->errorString();
        QByteArray body = reply_->readAll();
        reply_->deleteLater();
        reply_ = nullptr;
        emit busyChanged();

        if (!netOk && http != 304) {
            setStatus("Could not reach the series server: " + err);
            emit errorOccurred(err);
            emit updateAvailable(region, UpdateStatus::Error);
            return;
        }
        if (http == 304) {
            setStatus("Series list up to date.");
            emit seriesListUpdated();
            emit updateAvailable(region, UpdateStatus::UpToDate);
            return;
        }

        QJsonParseError pe;
        QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isArray()) {
            setStatus("Series list is not valid JSON.");
            emit errorOccurred(pe.errorString());
            emit updateAvailable(region, UpdateStatus::Error);
            return;
        }

        QVector<SeriesRow> parsed;
        for (const QJsonValue& v : doc.array()) {
            QJsonObject o = v.toObject();
            if (o.value("enabled").toBool(true) == false)
                continue;
            SeriesRow r;
            r.id   = o.value("_id").toString();
            r.set  = o.value("set").toString();
            r.name = o.value("name").toString();
            r.hash = o.value("hash").toString();
            if (!r.id.isEmpty())
                parsed.push_back(r);
        }

        saveSeriesToDb(region, parsed);
        if (!newEtag.isEmpty())
            Config::instance().setSeriesListEtag(region, QString::fromUtf8(newEtag));

        setStatus(QString("%1: series list updated · %2 sets").arg(tag).arg(parsed.size()));
        emit seriesListUpdated();
        emit updateAvailable(region, UpdateStatus::UpToDate);
    });
}

void SeriesRepository::downloadCardList(const QString& seriesId) {
    if (reply_) return;
    dlSeriesId_ = seriesId;
    setStatus("Downloading cards…");
    emit cardProgress(seriesId, 0.0);

    QString mutableId = seriesId;
    QNetworkRequest req{QUrl(Config::instance().getCardListUrl(mutableId))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    const QString etag = Config::instance().getCardListEtag(mutableId);
    if (!etag.isEmpty())
        req.setRawHeader("If-None-Match", etag.toUtf8());

    reply_ = nam_.get(req);
    emit busyChanged();

    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        emit cardProgress(dlSeriesId_, total > 0 ? double(got) / double(total) : 0.0);
    });

    connect(reply_, &QNetworkReply::finished, this, [this] {
        const int http = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray newEtag = reply_->rawHeader("ETag");
        const bool aborted = (reply_->error() == QNetworkReply::OperationCanceledError);
        const bool netOk   = (reply_->error() == QNetworkReply::NoError);
        const QString err  = reply_->errorString();
        QByteArray body = reply_->readAll();
        reply_->deleteLater();
        reply_ = nullptr;
        emit busyChanged();

        if (aborted) { setStatus("Download cancelled."); return; }

        if (http == 304) {
            setStatus("Cards already up to date.");
            emit cardProgress(dlSeriesId_, 1.0);
            emit cardListDownloaded(dlSeriesId_);
            return;
        }
        if (!netOk) { setStatus("Download failed: " + err); emit errorOccurred(err); return; }

        importCardListJson(dlSeriesId_, body);
        if (!newEtag.isEmpty())
            Config::instance().setCardListEtag(dlSeriesId_, QString::fromUtf8(newEtag));

        setStatus("Cards downloaded.");
        emit cardProgress(dlSeriesId_, 1.0);
        emit cardListDownloaded(dlSeriesId_);
    });
}

void SeriesRepository::importCardListJson(const QString& seriesId, const QByteArray& json) {
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isArray()) {
        emit errorOccurred("cardList is not valid JSON.");
        return;
    }
    QSqlDatabase db = cardDb();
    db.transaction();
    QSqlQuery del(db);
    del.prepare("DELETE FROM cards WHERE series_id = ?");
    del.addBindValue(seriesId);
    del.exec();

    QSqlQuery ins(db);
    ins.prepare("INSERT OR REPLACE INTO cards (series_id, card_id, cardcode, data) "
                "VALUES (?,?,?,?)");
    for (const QJsonValue& v : doc.array()) {
        QJsonObject o = v.toObject();
        ins.addBindValue(seriesId);
        ins.addBindValue(o.value("_id").toString());
        ins.addBindValue(o.value("cardcode").toString());
        ins.addBindValue(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        if (!ins.exec()) qWarning() << "card insert failed:" << ins.lastError().text();
    }
    db.commit();
}

void SeriesRepository::cancel() {
    if (reply_) reply_->abort();
    updateQueue_.clear();
    updatingAll_ = false;
}

void SeriesRepository::checkCardListUpdates()
{
    if (isCheckingCardLists())
        return;

    outdated_.clear();
    cardCheckQueue_.clear();
    for (const SeriesRow &r : loadSeries())
        if (r.hasCardList)
            cardCheckQueue_ << r.id;

    if (cardCheckQueue_.isEmpty()) {
        emit cardListCheckFinished(0);
        return;
    }

    setStatus(QString("Checking %1 card list(s) for updates…").arg(cardCheckQueue_.size()));
    for (int i = 0; i < 4; ++i)
        pumpCardCheckQueue();
}

void SeriesRepository::pumpCardCheckQueue()
{
    if (cardCheckQueue_.isEmpty()) {
        if (cardCheckActive_ == 0) {
            setStatus(outdated_.isEmpty()
                          ? QString("All card lists are up to date.")
                          : QString("%1 card list update(s) available.").arg(outdated_.size()));
            emit cardListCheckFinished(outdated_.size());
        }
        return;
    }

    QString id = cardCheckQueue_.takeFirst();
    const QString stored = Config::instance().getCardListEtag(id);

    QNetworkRequest req{QUrl(Config::instance().getCardListUrl(id))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "WSDeckImporter/1.0");
    if (!stored.isEmpty())
        req.setRawHeader("If-None-Match", stored.toUtf8());

    ++cardCheckActive_;
    QNetworkReply *head = nam_.head(req);
    const quint64 gen = cardCheckGen_;

    connect(head, &QNetworkReply::finished, this, [this, head, id, stored, gen] {
        head->deleteLater();
        if (gen != cardCheckGen_)
            return;
        --cardCheckActive_;

        const int http = head->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (head->error() == QNetworkReply::NoError || http == 304) {
            const QString remote = QString::fromUtf8(head->rawHeader("ETag"));
            const bool outdated = http != 304 && !remote.isEmpty()
                                  && (stored.isEmpty()
                                      || normalizeEtag(remote) != normalizeEtag(stored));
            if (outdated && !outdated_.contains(id))
                outdated_ << id;
            emit cardListUpdateState(id, outdated);
        }
        pumpCardCheckQueue();
    });
}

void SeriesRepository::updateOutdatedCardLists()
{
    if (isBusy() || updatingAll_ || outdated_.isEmpty())
        return;
    updateQueue_ = outdated_;
    updatingAll_ = true;
    pumpUpdateQueue();
}

void SeriesRepository::pumpUpdateQueue()
{
    if (!updatingAll_ || isBusy())
        return;
    if (updateQueue_.isEmpty()) {
        updatingAll_ = false;
        setStatus(outdated_.isEmpty()
                      ? QString("All card lists updated.")
                      : QString("%1 card list(s) could not be updated.").arg(outdated_.size()));
        return;
    }
    const QString id = updateQueue_.takeFirst();
    setStatus(QString("Updating card lists… (%1 left)").arg(updateQueue_.size() + 1));
    downloadCardList(id);
}

void SeriesRepository::resetSeries(Region region)
{
    QSqlQuery q(seriesDb(region));
    if (!q.exec("DROP TABLE IF EXISTS series"))
        qWarning() << "resetSeries failed:" << q.lastError().text();
    ensureSeriesSchema(region);
    Config::instance().setSeriesListEtag(region, "");
    setStatus(regionTag(region) + ": series list reset.");
    emit seriesListUpdated();
    emit updateAvailable(region, UpdateStatus::UpdateAvailable);
}

void SeriesRepository::resetCards(Region region)
{
    QSqlQuery q(cardDb(region));
    if (!q.exec("DROP TABLE IF EXISTS cards"))
        qWarning() << "resetCards failed:" << q.lastError().text();
    ensureCardSchema(region);
    Config::instance().clearCardListEtags(region);
    setStatus(regionTag(region) + ": card lists reset.");
    emit seriesListUpdated();
}

void SeriesRepository::purgeFallbackCards(Region region)
{
    QSqlQuery q(cardDb(region));
    if (q.exec("DELETE FROM cards WHERE series_id = '__official__'")) {
        const int n = q.numRowsAffected();
        setStatus(QString("%1: purged %2 fallback card(s).").arg(regionTag(region)).arg(n));
        emit fallbackCardsPurged(region, n);
        emit seriesListUpdated();
    } else {
        emit errorOccurred("Purge failed: " + q.lastError().text());
    }
}