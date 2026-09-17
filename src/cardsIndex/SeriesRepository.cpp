#include "SeriesRepository.h"
#include "../core/Config.h"

#include <QFile>
#include <QFileInfo>

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QSet>

static const char* kSeriesConn = "series_catalog_connection";
static const char* kCardConn   = "cardlist_catalog_connection";

SeriesRepository::SeriesRepository(QObject* parent) : QObject(parent) {
    ensureSeriesSchema();
    ensureCardSchema();
}

QSqlDatabase SeriesRepository::seriesDb() const {
    if (QSqlDatabase::contains(kSeriesConn)) {
        QSqlDatabase db = QSqlDatabase::database(kSeriesConn);
        if (!db.isOpen()) db.open();
        return db;
    }
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", kSeriesConn);
    db.setDatabaseName(Config::instance().getSeriesListDatabasePath());
    if (!db.open()) qCritical() << "seriesList.db open failed:" << db.lastError().text();
    return db;
}

QSqlDatabase SeriesRepository::cardDb() const {
    if (QSqlDatabase::contains(kCardConn)) {
        QSqlDatabase db = QSqlDatabase::database(kCardConn);
        if (!db.isOpen()) db.open();
        return db;
    }
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", kCardConn);
    db.setDatabaseName(Config::instance().getCardListDatabasePath());
    if (!db.open()) qCritical() << "cardList.db open failed:" << db.lastError().text();
    return db;
}

void SeriesRepository::ensureSeriesSchema() {
    QSqlQuery q(seriesDb());
    q.exec("CREATE TABLE IF NOT EXISTS series ("
           "  id TEXT PRIMARY KEY,"
           "  set_code TEXT,"
           "  name TEXT,"
           "  hash TEXT)");
}

void SeriesRepository::ensureCardSchema() {
    QSqlQuery q(cardDb());
    q.exec("CREATE TABLE IF NOT EXISTS cards ("
           "  series_id TEXT,"
           "  card_id   TEXT,"
           "  cardcode  TEXT,"
           "  data      TEXT,"
           "  PRIMARY KEY (series_id, card_id))");
    q.exec("CREATE INDEX IF NOT EXISTS idx_cards_cardcode ON cards(cardcode)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_cards_series ON cards(series_id)");
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

void SeriesRepository::saveSeriesToDb(const QVector<SeriesRow>& rows) {
    QSqlDatabase db = seriesDb();
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

void SeriesRepository::checkForUpdates() {
    setStatus("Checking for series list updates…");

    QNetworkRequest req{QUrl(Config::instance().getJpSeriesListUrl())};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "WSDeckImporter/1.0");

    QNetworkReply* head = nam_.head(req);
    connect(head, &QNetworkReply::finished, this, [this, head] {
        head->deleteLater();
        const QString dbPath = Config::instance().getSeriesListDatabasePath();
        const bool dbExists = QFile::exists(dbPath) && QFileInfo(dbPath).size() != 0;

        if (head->error() != QNetworkReply::NoError) {
            if (dbExists) { setStatus("Server unreachable. Using local series list."); emit updateAvailable(UpdateStatus::UpToDate); }
            else          { setStatus("Cannot reach server and no local series list."); emit updateAvailable(UpdateStatus::Error); }
            return;
        }

        const QString remoteEtag = QString::fromUtf8(head->rawHeader("ETag"));
        const QString cachedEtag = Config::instance().getJpSeriesListEtag();

        if (!dbExists) {
            setStatus("No local series list. Download needed.");
            emit updateAvailable(UpdateStatus::Error);
        } else if (!remoteEtag.isEmpty() && remoteEtag == cachedEtag) {
            setStatus("Series list is up to date.");
            emit updateAvailable(UpdateStatus::UpToDate);
        } else {
            setStatus("Series list update available.");
            emit updateAvailable(UpdateStatus::UpdateAvailable);
        }
    });
}

void SeriesRepository::refreshSeriesList() {
    if (reply_) return;
    setStatus("Checking series list…");

    QNetworkRequest req{QUrl(Config::instance().getJpSeriesListUrl())};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    const QString etag = Config::instance().getJpSeriesListEtag();
    if (!etag.isEmpty())
        req.setRawHeader("If-None-Match", etag.toUtf8());

    reply_ = nam_.get(req);
    emit busyChanged();

    connect(reply_, &QNetworkReply::downloadProgress, this,
            [this](qint64 got, qint64 total) { emit seriesProgress(got, total); });

    connect(reply_, &QNetworkReply::finished, this, [this] {
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
            emit updateAvailable(UpdateStatus::Error);
            return;
        }
        if (http == 304) {
            setStatus("Series list up to date.");
            emit seriesListUpdated();
            emit updateAvailable(UpdateStatus::UpToDate);
            return;
        }

        QJsonParseError pe;
        QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isArray()) {
            setStatus("Series list is not valid JSON.");
            emit errorOccurred(pe.errorString());
            emit updateAvailable(UpdateStatus::Error);
            return;
        }

        QVector<SeriesRow> parsed;
        for (const QJsonValue& v : doc.array()) {
            QJsonObject o = v.toObject();
            if (o.value("enabled").toBool(true) == false) continue;
            SeriesRow r;
            r.id   = o.value("_id").toString();
            r.set  = o.value("set").toString();
            r.name = o.value("name").toString();
            r.hash = o.value("hash").toString();
            if (!r.id.isEmpty()) parsed.push_back(r);
        }

        saveSeriesToDb(parsed);
        if (!newEtag.isEmpty())
            Config::instance().setJpSeriestListEtag(QString::fromUtf8(newEtag));

        setStatus(QString("Series list updated · %1 sets").arg(parsed.size()));
        emit seriesListUpdated();
        emit updateAvailable(UpdateStatus::UpToDate);
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
}

void SeriesRepository::resetSeries() {
    QSqlQuery q(seriesDb());
    if (!q.exec("DROP TABLE IF EXISTS series"))
        qWarning() << "resetSeries failed:" << q.lastError().text();
    ensureSeriesSchema();
    Config::instance().setJpSeriestListEtag("");
    setStatus("Series list reset.");
    emit seriesListUpdated();
    emit updateAvailable(UpdateStatus::UpdateAvailable);
}

void SeriesRepository::resetCards() {
    QSqlQuery q(cardDb());
    if (!q.exec("DROP TABLE IF EXISTS cards"))
        qWarning() << "resetCards failed:" << q.lastError().text();
    ensureCardSchema();
    Config::instance().clearCardListEtags();
    setStatus("Card lists reset.");
    emit seriesListUpdated();
}

void SeriesRepository::purgeFallbackCards()
{
    QSqlDatabase db = cardDb();
    QSqlQuery q(db);
    q.prepare("DELETE FROM cards WHERE series_id = '__official__'");
    if (q.exec()) {
        int n = q.numRowsAffected();
        setStatus(QString("Purged %1 fallback card(s).").arg(n));
        emit fallbackCardsPurged(n);
        emit seriesListUpdated();
    } else {
        emit errorOccurred("Purge failed: " + q.lastError().text());
    }
}