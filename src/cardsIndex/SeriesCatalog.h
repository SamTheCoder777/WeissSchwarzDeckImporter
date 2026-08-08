#pragma once
// SeriesCatalog — downloads the encoredecks JP "series list", stores it in a
// local SQLite table (seriesList.db), tracks the list's ETag so we only
// re-download when it changed, and exposes each series as a model row with a
// per-row "download cardList" action.
//
// Series JSON object shape (from getJpSeriesListUrl):
//   { "_id":"5c67...", "set":"AW", "name":"Accel World", "hash":"5d22...",
//     "side":"S", "release":"18", "game":"WS", "lang":"JP", "enabled":true }
//
// We persist: _id, set, name, hash  (id is what we need to fetch a cardList).

#include <QAbstractListModel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSqlDatabase>
#include <QVector>
#include <QString>
#include <QSet>

class SeriesCatalog : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(bool    busy   READ busy   NOTIFY stateChanged)

public:
    // cardList install state, per series
    enum Status { NotDownloaded = 0, Downloaded = 1 };
    Q_ENUM(Status)

    enum Roles {
        IdRole = Qt::UserRole + 1,   // _id
        SetRole,                     // "AW"
        NameRole,                    // "Accel World"
        HashRole,                    // series hash
        StatusRole,                  // NotDownloaded / Downloaded
        DownloadingRole,             // is downloading
        ProgressRole                 // 0..1 for the active cardList download
    };

    explicit SeriesCatalog(QObject* parent = nullptr);

    int rowCount(const QModelIndex& = {}) const override { return rows_.size(); }
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString status() const { return status_; }
    bool    busy()   const { return reply_ != nullptr; }

    Q_INVOKABLE void refreshSeriesList();
    Q_INVOKABLE void downloadCardList(const QString& seriesId);
    Q_INVOKABLE void cancel();

signals:
    void stateChanged();
    void errorOccurred(const QString& message);
    void refreshFinished();
    void cardListDownloaded(const QString& seriesId);

private:
    struct Row {
        QString id, set, name, hash;
        bool    hasCardList = false;
        bool    downloading = false;
        double  progress = 0.0;
    };

    void setStatus(const QString& s) { status_ = s; emit stateChanged(); }
    void touchRow(int row);
    int  rowForId(const QString& id) const;

    QSqlDatabase seriesDb() const;
    void ensureSeriesSchema();
    void loadRowsFromDb();
    void saveSeriesToDb(const QVector<Row>& rows);
    void markDownloadedFromCardDb();

    QSqlDatabase cardDb() const;
    void ensureCardSchema();
    void importCardListJson(const QString& seriesId, const QByteArray& json);

    QVector<Row> rows_;
    QString status_ = "Ready.";

    QNetworkAccessManager nam_;
    QNetworkReply* reply_ = nullptr;

    QString dlSeriesId_;
};