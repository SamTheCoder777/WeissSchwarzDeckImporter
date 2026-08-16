#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include "SeriesRepository.h"

class SeriesCatalog : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(bool    busy   READ busy   NOTIFY stateChanged)

public:
    enum Status { NotDownloaded = 0, Downloaded = 1 };
    Q_ENUM(Status)

    enum Roles {
        IdRole = Qt::UserRole + 1,
        SetRole, NameRole, HashRole,
        StatusRole, DownloadingRole, ProgressRole
    };

    explicit SeriesCatalog(SeriesRepository* repo, QObject* parent = nullptr);

    int rowCount(const QModelIndex& = {}) const override { return rows_.size(); }
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString status() const { return repo_ ? repo_->status() : QString(); }
    bool    busy()   const { return repo_ && repo_->isBusy(); }

    Q_INVOKABLE void refreshSeriesList() { if (repo_) repo_->refreshSeriesList(); }
    Q_INVOKABLE void downloadCardList(const QString& seriesId) { if (repo_) repo_->downloadCardList(seriesId); }
    Q_INVOKABLE void cancel() { if (repo_) repo_->cancel(); }

signals:
    void stateChanged();
    void errorOccurred(const QString& message);
    void cardListDownloaded(const QString& seriesId);

private:
    struct Row {
        QString id, set, name, hash;
        bool    hasCardList = false;
        bool    downloading = false;
        double  progress = 0.0;
    };

    void reloadFromRepo();
    int  rowForId(const QString& id) const;
    void touchRow(int row);

    SeriesRepository* repo_ = nullptr;
    QVector<Row> rows_;
};
