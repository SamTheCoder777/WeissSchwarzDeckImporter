#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSqlDatabase>
#include <QVector>
#include <QString>

class SeriesRepository : public QObject {
    Q_OBJECT
public:

    struct SeriesRow {
        QString id, set, name, hash;
        bool    hasCardList = false;
    };

    enum class UpdateStatus { UpToDate, UpdateAvailable, Error };
    Q_ENUM(UpdateStatus)

    explicit SeriesRepository(QObject* parent = nullptr);

    // read
    QVector<SeriesRow> loadSeries();
    bool   isBusy() const { return reply_ != nullptr; }
    QString status() const { return status_; }

    // actions
    void checkForUpdates();
    void refreshSeriesList();
    void downloadCardList(const QString& seriesId);
    void cancel();
    void resetSeries();
    void resetCards();
    void purgeFallbackCards();

signals:
    void statusChanged(const QString& s);
    void errorOccurred(const QString& message);
    void seriesListUpdated();
    void cardListDownloaded(const QString& seriesId);
    void cardProgress(const QString& seriesId, double progress);
    void seriesProgress(qint64 received, qint64 total);
    void updateAvailable(SeriesRepository::UpdateStatus status);
    void busyChanged();
    void fallbackCardsPurged(int count);

private:
    QSqlDatabase seriesDb() const;
    QSqlDatabase cardDb() const;
    void ensureSeriesSchema();
    void ensureCardSchema();
    void saveSeriesToDb(const QVector<SeriesRow>& rows);
    void importCardListJson(const QString& seriesId, const QByteArray& json);
    void setStatus(const QString& s) { status_ = s; emit statusChanged(s); }

    QNetworkAccessManager nam_;
    QNetworkReply* reply_ = nullptr;
    QString status_ = "Ready.";
    QString dlSeriesId_;
};