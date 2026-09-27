#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include "SeriesRepository.h"

class SeriesCatalog : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(int outdatedCount READ outdatedCount NOTIFY stateChanged)
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY stateChanged)
    Q_PROPERTY(bool updatingAll READ updatingAll NOTIFY stateChanged)
public:
    enum Status { NotDownloaded = 0, Downloaded = 1, UpdateAvailable = 2 };
    Q_ENUM(Status)

    enum Roles {
        IdRole = Qt::UserRole + 1,
        SetRole,
        NameRole,
        HashRole,
        StatusRole,
        DownloadingRole,
        ProgressRole
    };

    explicit SeriesCatalog(SeriesRepository *repo, QObject *parent = nullptr);

    int rowCount(const QModelIndex & = {}) const override { return rows_.size(); }
    QVariant data(const QModelIndex &idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString status() const { return repo_ ? repo_->status() : QString(); }
    bool busy() const { return repo_ && repo_->isBusy(); }
    bool checkingUpdates() const { return repo_ && repo_->isCheckingCardLists(); }
    bool updatingAll() const { return repo_ && repo_->isUpdatingAll(); }

    Q_INVOKABLE void refreshSeriesList()
    {
        if (repo_)
            repo_->refreshSeriesList();
    }
    Q_INVOKABLE void downloadCardList(const QString &seriesId)
    {
        if (repo_)
            repo_->downloadCardList(seriesId);
    }
    Q_INVOKABLE void cancel()
    {
        if (repo_)
            repo_->cancel();
    }

    int outdatedCount() const { return outdatedIds_.size(); }
    Q_INVOKABLE void checkCardListUpdates()
    {
        if (repo_)
            repo_->checkCardListUpdates();
    }
    Q_INVOKABLE void updateOutdatedCardLists()
    {
        if (repo_)
            repo_->updateOutdatedCardLists();
    }

    Q_INVOKABLE void checkForUpdatesFor(const QString &region)
    {
        if (repo_)
            repo_->checkForUpdates(region == "EN" ? SeriesRepository::Region::EN
                                                  : SeriesRepository::Region::JP);
    }
    Q_INVOKABLE void refreshSeriesListFor(const QString &region)
    {
        if (repo_)
            repo_->refreshSeriesList(region == "EN" ? SeriesRepository::Region::EN
                                                    : SeriesRepository::Region::JP);
    }

signals:
    void stateChanged();
    void errorOccurred(const QString &message);
    void cardListDownloaded(const QString &seriesId);

private:
    struct Row
    {
        QString id, set, name, hash;
        bool hasCardList = false;
        bool downloading = false;
        double progress = 0.0;
        bool updateAvailable = false;
    };

    void reloadFromRepo();
    int rowForId(const QString &id) const;
    void touchRow(int row);

    SeriesRepository *repo_ = nullptr;
    QVector<Row> rows_;

    QSet<QString> outdatedIds_;
};
