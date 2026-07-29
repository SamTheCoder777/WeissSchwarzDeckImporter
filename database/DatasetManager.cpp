#include "DatasetManager.h"
#include "../Config.h"

#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QSqlQuery>

DatasetManager::DatasetManager(const QUrl &datasetUrl, QObject *parent):
    datasetUrl_(datasetUrl), QObject(parent){}


void DatasetManager::startDownloadAndImport() {
    isDownloading_ = true;
    qRegisterMetaType<QByteArrayList>("QByteArrayList");

    worker_ = new DatabaseWorker();
    worker_->moveToThread(&workerThread_);

    connect(&workerThread_, &QThread::started, worker_, [this]() {
        QMetaObject::invokeMethod(worker_, "initDatabase", Q_ARG(bool, true));
    });

    connect(worker_, &DatabaseWorker::finished, &workerThread_, &QThread::quit);
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);

    connect(worker_, &DatabaseWorker::finished, this, [this]() {
        isDownloading_ = false;
        Config::instance().setCurDatasetEtag(remoteEtag_);
        emit readyToUse();
        emit statusChanged("Database successfully updated!");
    });

    workerThread_.start();

    QNetworkReply *reply = netManager_.get(QNetworkRequest(datasetUrl_));

    // Connect network progress signal directly
    connect(reply, &QNetworkReply::downloadProgress, this, &DatasetManager::downloadProgress);

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        streamBuffer_.append(reply->readAll());
        QByteArray data;
        int idx = 0;
        while ((idx = streamBuffer_.indexOf('\n')) != -1) {
            data.append(streamBuffer_.left(idx));
            streamBuffer_.remove(0, idx + 1);
        }
        if (!data.isEmpty()) {
            QMetaObject::invokeMethod(worker_, "processChunk",
                                      Qt::QueuedConnection,
                                      Q_ARG(QByteArray, data));
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            streamBuffer_.append(reply->readAll());
            QByteArray data;
            int idx = 0;
            while ((idx = streamBuffer_.indexOf('\n')) != -1) {
                data.append(streamBuffer_.left(idx));
                streamBuffer_.remove(0, idx + 1);
            }
            if (!streamBuffer_.trimmed().isEmpty()) {
                data.append(streamBuffer_);
                streamBuffer_.clear();
            }
            if (!data.isEmpty()) {
                QMetaObject::invokeMethod(worker_, "processChunk",
                                          Qt::QueuedConnection,
                                          Q_ARG(QByteArray, data));
            }
            QMetaObject::invokeMethod(worker_, "finishProcessing", Qt::QueuedConnection);
        } else {
            isDownloading_ = false;
            emit statusChanged("Download failed: " + reply->errorString());
            workerThread_.quit();
        }
        reply->deleteLater();
    });
}

void DatasetManager::checkAndLoad(bool forceRedownload) {
    if (isDownloading_) {
        emit statusChanged("Download is already running in background...");
        return;
    }

    emit statusChanged("Checking server for updates...");

    QNetworkRequest request(datasetUrl_);

    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = netManager_.head(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, forceRedownload]() {
        if (reply->error() == QNetworkReply::NoError) {
            remoteEtag_ = reply->rawHeader("ETag");

            QString cachedEtag = Config::instance().getCurDatasetEtag();
            bool dbExists = QFile::exists("dataset_cache.db");

            if (!forceRedownload && dbExists && !remoteEtag_.isEmpty() && remoteEtag_ == cachedEtag) {
                emit statusChanged("Database is up to date.");
                emit readyToUse();
            } else {
                emit statusChanged("Downloading and processing dataset...");
                startDownloadAndImport();
            }
        } else {
            if (QFile::exists("dataset_cache.db")) {
                emit statusChanged("Server unreachable. Operating in offline mode.");
                emit readyToUse();
            } else {
                emit statusChanged("Failed to check updates and no local database found.");
            }
        }
        reply->deleteLater();
    });
}

int DatasetManager::getLocalRowCount() {
    if (!QFile::exists("dataset_cache.db")) return 0;

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "info_connection");
    db.setDatabaseName("dataset_cache.db");
    int count = 0;
    if (db.open()) {
        QSqlQuery q("SELECT COUNT(*) FROM dataset", db);
        if (q.next()) count = q.value(0).toInt();
        db.close();
    }
    QSqlDatabase::removeDatabase("info_connection");
    return count;
}

double DatasetManager::getDatabaseSizeMB() {
    QFileInfo info("dataset_cache.db");
    if (!info.exists()) return 0.0;
    return info.size() / (1024.0 * 1024.0);
}
