#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include "../core/Config.h"

class SeriesRepository : public QObject {
    Q_OBJECT
public:
    using Region = Config::DetectLocaleMode;

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
    bool isUpdatingAll() const { return updatingAll_; }

    // actions
    void downloadCardList(const QString& seriesId);
    void cancel();
    void checkCardListUpdates();
    void updateOutdatedCardLists();
    bool isCheckingCardLists() const { return cardCheckActive_ > 0 || !cardCheckQueue_.isEmpty(); }

    void resetCards(Region region);
    void resetCards() { resetCards(currentRegion()); }
    void purgeFallbackCards(Region region);
    void purgeFallbackCards() { purgeFallbackCards(currentRegion()); }

    void checkForUpdates(Region region);
    void checkForUpdates() { checkForUpdates(currentRegion()); }
    void refreshSeriesList(Region region);
    void refreshSeriesList() { refreshSeriesList(currentRegion()); }
    void resetSeries(Region region);
    void resetSeries() { resetSeries(currentRegion()); }

signals:
    void statusChanged(const QString& s);
    void errorOccurred(const QString& message);
    void seriesListUpdated();
    void cardListDownloaded(const QString& seriesId);
    void cardProgress(const QString &seriesId, double progress);
    void updateAvailable(Config::DetectLocaleMode region, SeriesRepository::UpdateStatus status);
    void busyChanged();
    void seriesProgress(Config::DetectLocaleMode region, qint64 received, qint64 total);
    void fallbackCardsPurged(Config::DetectLocaleMode region, int count);
    void cardListUpdateState(const QString &seriesId, bool updateAvailable);
    void cardListCheckFinished(int outdatedCount);

private:
    QSqlDatabase cardDb(Region region) const;
    QSqlDatabase cardDb() const { return cardDb(currentRegion()); }
    void ensureCardSchema(Region region);
    void ensureCardSchema() { ensureCardSchema(currentRegion()); }
    void importCardListJson(const QString& seriesId, const QByteArray& json);
    void setStatus(const QString& s) { status_ = s; emit statusChanged(s); }

    static Region currentRegion() { return Config::instance().getCurDetectLocaleMode(); }
    static QString regionTag(Region r) { return r == Region::JP ? "JP" : "EN"; }

    QSqlDatabase seriesDb(Region region) const;
    QSqlDatabase seriesDb() const { return seriesDb(currentRegion()); }
    bool hasLocalSeries(Region region) const;
    void ensureSeriesSchema(Region region);
    void saveSeriesToDb(Region region, const QVector<SeriesRow> &rows);

    QNetworkAccessManager nam_;
    QNetworkReply* reply_ = nullptr;
    QString status_ = "Ready.";
    QString dlSeriesId_;

    void pumpCardCheckQueue();
    void pumpUpdateQueue();

    QStringList cardCheckQueue_;
    int cardCheckActive_ = 0;
    quint64 cardCheckGen_ = 0;
    QStringList outdated_;
    QStringList updateQueue_;
    bool updatingAll_ = false;
};