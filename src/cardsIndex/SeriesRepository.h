#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>
#include <QUrl>
#include <QByteArray>
#include <QVector>
#include "../core/Config.h"

class SeriesRepository : public QObject {
    Q_OBJECT
public:
    using Region = Config::DetectLocaleMode;
    struct SeriesRow {
        QString id, set, name, hash;
        bool hasCardList = false;
        bool updateAvailable = false;
    };
    enum class UpdateStatus { UpToDate, UpdateAvailable, Error };
    Q_ENUM(UpdateStatus)

    explicit SeriesRepository(QObject *parent = nullptr);
    ~SeriesRepository() override;

    QVector<SeriesRow> loadSeries();
    bool isBusy() const { return requestId_ != 0; }
    QString status() const { return status_; }
    bool isUpdatingAll() const { return updatingAll_; }
    bool isCheckingCardLists() const { return checkingCards_; }

    void checkAutomaticUpdates();
    void downloadCardList(const QString &seriesId);
    void cancel();
    void checkCardListUpdates(bool manual = false);
    void updateOutdatedCardLists();
    void checkForUpdates(Region region, bool manual = false);
    void checkForUpdates() { checkForUpdates(currentRegion(), false); }
    void refreshSeriesList(Region region, bool manual = false);
    void refreshSeriesList() { refreshSeriesList(currentRegion(), false); }
    void resetSeries(Region region);
    void resetSeries() { resetSeries(currentRegion()); }
    void resetCards(Region region);
    void resetCards() { resetCards(currentRegion()); }
    void purgeFallbackCards(Region region);
    void purgeFallbackCards() { purgeFallbackCards(currentRegion()); }

signals:
    void statusChanged(const QString &s);
    void errorOccurred(const QString &message);
    void seriesListUpdated();
    void cardListDownloaded(const QString &seriesId);
    void cardDownloadFinished(const QString &seriesId, bool success);
    void cardProgress(const QString &seriesId, double progress);
    void updateAvailable(Config::DetectLocaleMode region, UpdateStatus status);
    void busyChanged();
    void checkStateChanged();
    void updateAllStateChanged();
    void seriesProgress(Config::DetectLocaleMode region, qint64 received, qint64 total);
    void fallbackCardsPurged(Config::DetectLocaleMode region, int count);
    void cardListUpdateState(const QString &seriesId, bool updateAvailable);
    void cardListCheckFinished(int outdatedCount);

private:
    static Region currentRegion() { return Config::instance().getCurDetectLocaleMode(); }
    static QString regionTag(Region region) { return region == Region::JP ? "JP" : "EN"; }
    void setStatus(const QString &s) { status_ = s; emit statusChanged(s); }
    QString checkKey(const QString &kind, Region region, const QString &identity) const;
    bool checkAllowed(const QString &key, const QUrl &url, bool manual);
    bool serverAllowed(const QUrl &url);
    void invalidateNetwork();
    void stopUpdateBatch();
    void pumpCardCheckQueue();
    void pumpUpdateQueue();
    void finishCardChecks();
    void restoreOutdatedState();

    QSqlDatabase cardDb(Region region) const;
    QSqlDatabase cardDb() const { return cardDb(currentRegion()); }
    QSqlDatabase seriesDb(Region region) const;
    QSqlDatabase seriesDb() const { return seriesDb(currentRegion()); }
    void ensureCardSchema(Region region);
    void ensureSeriesSchema(Region region);
    bool hasLocalSeries(Region region) const;
    bool hasLocalCards(Region region, const QString &seriesId) const;
    bool saveSeriesToDb(Region region, const QVector<SeriesRow> &rows, QString *error);
    bool importCardListJson(Region region, const QString &seriesId,
                            const QByteArray &json, QString *error);
    bool storeCardUpdateState(Region region, const QString &id,
                              bool outdated, QString *error);

    quint64 requestId_ = 0;
    quint64 epoch_ = 0;
    QHash<int, quint64> seriesHeadIds_;
    QSet<quint64> cardCheckIds_;
    QString activeCardId_;
    QString status_ = "Ready.";
    QStringList cardCheckQueue_;
    QStringList outdated_;
    QStringList updateQueue_;
    int cardCheckActive_ = 0;
    bool checkingCards_ = false;
    bool cardCheckHadErrors_ = false;
    bool cardBatchRecorded_ = false;
    QString cardBatchKey_;
    bool updatingAll_ = false;
};
