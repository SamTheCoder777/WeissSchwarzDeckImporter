#pragma once

#include "DatabaseWorker.h"
#include "../core/Config.h"

#include <QNetworkAccessManager>
#include <QThread>
#include <QUrl>


class DatasetManager : public QObject {
    Q_OBJECT

private:
    QUrl datasetUrl_;
    QNetworkAccessManager netManager_;
    QThread workerThread_;
    DatabaseWorker *worker_ = nullptr;
    QString remoteEtag_;
    QByteArray streamBuffer_;
    bool isDownloading_ = false;

    DatabaseWorker::DatabaseMode curMode_;

public:
    explicit DatasetManager(const DatabaseWorker::DatabaseMode mode, QObject *parent = nullptr);

    ~DatasetManager() {
        if (workerThread_.isRunning()) {
            workerThread_.quit();
            workerThread_.wait();
        }
    }

    enum class UpdateStatus {
        UpdateAvailable,
        UpToDate,
        Error
    };


    QSqlDatabase getUiDatabase();
    QSqlDatabase getCardListDatabase();

    void setDatasetUrl_(const QString &url){ datasetUrl_ = url; }

    bool isDownloading() const { return isDownloading_; }

    void checkAndLoad(bool forceRedownload = false);
    void checkForUpdates();
    void startDownloadAndImport();

    int getLocalRowCount();

    double getDatabaseSizeMB();

signals:
    void readyToUse();
    void updateAvailable(DatasetManager::UpdateStatus status, const QString &newVersion);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void statusChanged(const QString &statusText);
};