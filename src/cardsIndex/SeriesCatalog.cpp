#include "SeriesCatalog.h"
#include "../core/Config.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QFileInfo>

static const char* kSeriesConn = "series_catalog_connection";
static const char* kCardConn   = "cardlist_catalog_connection";

SeriesCatalog::SeriesCatalog(QObject* parent) : QAbstractListModel(parent) {
    ensureSeriesSchema();
    ensureCardSchema();
    loadRowsFromDb();
    markDownloadedFromCardDb();
}

QSqlDatabase SeriesCatalog::seriesDb() const {
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

QSqlDatabase SeriesCatalog::cardDb() const {
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

void SeriesCatalog::ensureSeriesSchema() {
    QSqlQuery q(seriesDb());
    q.exec("CREATE TABLE IF NOT EXISTS series ("
           "  id TEXT PRIMARY KEY,"
           "  set_code TEXT,"
           "  name TEXT,"
           "  hash TEXT)");
}

void SeriesCatalog::ensureCardSchema() {

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


QVariant SeriesCatalog::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() >= rows_.size()) return {};
    const Row& r = rows_[idx.row()];
    switch (role) {
    case IdRole:          return r.id;
    case SetRole:         return r.set;
    case NameRole:        return r.name;
    case HashRole:        return r.hash;
    case StatusRole:      return r.hasCardList ? (int)Downloaded : (int)NotDownloaded;
    case DownloadingRole: return r.downloading;
    case ProgressRole:    return r.progress;
    }
    return {};
}

QHash<int, QByteArray> SeriesCatalog::roleNames() const {
    return {{IdRole,"idStr"}, {SetRole,"setCode"}, {NameRole,"name"},
            {HashRole,"hash"}, {StatusRole,"statusCode"},
            {DownloadingRole,"downloading"}, {ProgressRole,"progress"}};
}

void SeriesCatalog::touchRow(int row) {
    if (row >= 0 && row < rows_.size())
        emit dataChanged(index(row), index(row));
}

int SeriesCatalog::rowForId(const QString& id) const {
    for (int i = 0; i < rows_.size(); ++i) if (rows_[i].id == id) return i;
    return -1;
}


void SeriesCatalog::loadRowsFromDb() {
    QVector<Row> loaded;
    QSqlQuery q(seriesDb());
    if (q.exec("SELECT id, set_code, name, hash FROM series ORDER BY name")) {
        while (q.next()) {
            Row r;
            r.id   = q.value(0).toString();
            r.set  = q.value(1).toString();
            r.name = q.value(2).toString();
            r.hash = q.value(3).toString();
            loaded.push_back(r);
        }
    }
    beginResetModel();
    rows_ = loaded;
    endResetModel();
}

void SeriesCatalog::saveSeriesToDb(const QVector<Row>& rows) {
    QSqlDatabase db = seriesDb();
    db.transaction();
    QSqlQuery q(db);
    q.exec("DELETE FROM series");
    q.prepare("INSERT INTO series (id, set_code, name, hash) VALUES (?,?,?,?)");
    for (const Row& r : rows) {
        q.addBindValue(r.id);
        q.addBindValue(r.set);
        q.addBindValue(r.name);
        q.addBindValue(r.hash);
        if (!q.exec()) qWarning() << "series insert failed:" << q.lastError().text();
    }
    db.commit();
}

void SeriesCatalog::markDownloadedFromCardDb() {

    QSet<QString> have;
    QSqlQuery q(cardDb());
    if (q.exec("SELECT DISTINCT series_id FROM cards"))
        while (q.next()) have.insert(q.value(0).toString());

    for (int i = 0; i < rows_.size(); ++i) {
        bool now = have.contains(rows_[i].id);
        if (rows_[i].hasCardList != now) { rows_[i].hasCardList = now; touchRow(i); }
    }
}


void SeriesCatalog::refreshSeriesList() {
    if (reply_) return;
    setStatus("Checking series list…");

    QNetworkRequest req{QUrl(Config::instance().getJpSeriesListUrl())};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    const QString etag = Config::instance().getJpSeriesListEtag();
    if (!etag.isEmpty())
        req.setRawHeader("If-None-Match", etag.toUtf8());

    reply_ = nam_.get(req);
    emit stateChanged();

    connect(reply_, &QNetworkReply::finished, this, [this] {
        const int http = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray newEtag = reply_->rawHeader("ETag");
        const bool netOk = (reply_->error() == QNetworkReply::NoError);
        const QString err = reply_->errorString();
        QByteArray body = reply_->readAll();
        reply_->deleteLater();
        reply_ = nullptr;
        emit stateChanged();

        if (!netOk && http != 304) {

            setStatus("Could not reach the series server: " + err);
            emit errorOccurred(err);
            return;
        }

        if (http == 304) {
            setStatus(QString("Series list up to date · %1 sets").arg(rows_.size()));
            emit refreshFinished();
            return;
        }

        QJsonParseError pe;
        QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isArray()) {
            setStatus("Series list is not valid JSON.");
            emit errorOccurred(pe.errorString());
            return;
        }

        QVector<Row> parsed;
        for (const QJsonValue& v : doc.array()) {
            QJsonObject o = v.toObject();

            if (o.value("enabled").toBool(true) == false) continue;
            Row r;
            r.id   = o.value("_id").toString();
            r.set  = o.value("set").toString();
            r.name = o.value("name").toString();
            r.hash = o.value("hash").toString();
            if (!r.id.isEmpty()) parsed.push_back(r);
        }

        saveSeriesToDb(parsed);
        if (!newEtag.isEmpty())
            Config::instance().setJpSeriestListEtag(QString::fromUtf8(newEtag));

        loadRowsFromDb();
        markDownloadedFromCardDb();
        setStatus(QString("Series list updated · %1 sets").arg(rows_.size()));
        emit refreshFinished();
    });
}


void SeriesCatalog::downloadCardList(const QString& seriesId) {
    if (reply_) return;
    int row = rowForId(seriesId);
    if (row < 0) return;

    dlSeriesId_ = seriesId;
    rows_[row].downloading = true;
    rows_[row].progress = 0.0;
    touchRow(row);
    setStatus("Downloading cards for " + rows_[row].name + "…");

    QString mutableId = seriesId;
    QNetworkRequest req{QUrl(Config::instance().getCardListUrl(mutableId))};

    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    const QString etag = Config::instance().getCardListEtag(mutableId);
    if (!etag.isEmpty())
        req.setRawHeader("If-None-Match", etag.toUtf8());

    reply_ = nam_.get(req);
    emit stateChanged();

    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        int row = rowForId(dlSeriesId_);
        if (row < 0) return;
        rows_[row].progress = (total > 0) ? double(got) / double(total) : 0.0;
        touchRow(row);
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
        emit stateChanged();

        int row = rowForId(dlSeriesId_);
        if (row >= 0) { rows_[row].downloading = false; touchRow(row); }

        if (aborted) { setStatus("Download cancelled."); return; }

        if (http == 304) {
            if (row >= 0) { rows_[row].hasCardList = true; rows_[row].progress = 1.0; touchRow(row); }
            setStatus("Cards already up to date.");
            emit cardListDownloaded(dlSeriesId_);
            return;
        }

        if (!netOk) { setStatus("Download failed: " + err); emit errorOccurred(err); return; }

        importCardListJson(dlSeriesId_, body);
        if (!newEtag.isEmpty())
            Config::instance().setCardListEtag(dlSeriesId_, QString::fromUtf8(newEtag));

        if (row >= 0) { rows_[row].hasCardList = true; rows_[row].progress = 1.0; touchRow(row); }
        setStatus("Cards downloaded.");
        emit cardListDownloaded(dlSeriesId_);
    });
}

void SeriesCatalog::importCardListJson(const QString& seriesId, const QByteArray& json) {
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

        QString cardId   = o.value("_id").toString();
        QString cardcode = o.value("cardcode").toString();
        ins.addBindValue(seriesId);
        ins.addBindValue(cardId);
        ins.addBindValue(cardcode);
        ins.addBindValue(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
        if (!ins.exec()) qWarning() << "card insert failed:" << ins.lastError().text();
    }
    db.commit();
}

void SeriesCatalog::cancel() {
    if (reply_) reply_->abort();
}