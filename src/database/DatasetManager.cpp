#include "DatasetManager.h"
#include "../core/Config.h"

#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QSqlQuery>
#include <QSqlError>

DatasetManager::DatasetManager(const QUrl &datasetUrl, QObject *parent):
    datasetUrl_(datasetUrl), QObject(parent){}

QSqlDatabase DatasetManager::getUiDatabase() {
    const QString connName = "main_ui_connection";

    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase db = QSqlDatabase::database(connName);
        if (!db.isOpen()) {
            if (!db.open()) {
                qCritical() << "Failed to reopen UI database:" << db.lastError().text();
            }
        }
        return db;
    }

    // First time setup
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setDatabaseName(Config::instance().getDatasetPath());

    if (!db.open()) {
        qCritical() << "Failed to open UI database:" << db.lastError().text();
    }

    return db;
}


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

    QNetworkRequest request(datasetUrl_);

    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);

    QNetworkReply *reply = netManager_.get(request);

    // Connect network progress signal directly
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 bytesReceived, qint64 bytesTotal){
        qDebug() << "[DatasetManager] download progress" << bytesReceived << "/" << bytesTotal;
        emit downloadProgress(bytesReceived, bytesTotal);
    });

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

void DatasetManager::checkForUpdates(){
    emit statusChanged("Checking for dataset updates...");

    QNetworkRequest request(datasetUrl_);

    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = netManager_.head(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            remoteEtag_ = reply->rawHeader("ETag");

            QString cachedEtag = Config::instance().getCurDatasetEtag();
            bool dbExists = QFile::exists(Config::instance().getDatasetPath())
                && QFileInfo(Config::instance().getDatasetPath()).size() != 0;

            if (dbExists && !remoteEtag_.isEmpty() && remoteEtag_ == cachedEtag) {
                emit statusChanged("Database is up to date.");
                emit updateAvailable(DatasetManager::UpdateStatus::UpToDate, "New Version");
                emit readyToUse();
            }
            else if(!dbExists){
                emit statusChanged("Database broken or missing! Please redownload.");
                emit updateAvailable(DatasetManager::UpdateStatus::Error, "New Version");
            }
            else {
                emit statusChanged("Update available!");
                emit updateAvailable(DatasetManager::UpdateStatus::UpdateAvailable, "New Version"); //TODO Later replace new version with actual version num if possible
            }
        } else {
            if (QFile::exists(Config::instance().getDatasetPath())) {
                emit statusChanged("Server unreachable. Operating in offline mode.");
                emit readyToUse();
            } else {
                emit statusChanged("Failed to check updates and no local database found.");
            }
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
            bool dbExists = QFile::exists(Config::instance().getDatasetPath());

            if (!forceRedownload && dbExists && !remoteEtag_.isEmpty() && remoteEtag_ == cachedEtag) {
                emit statusChanged("Database is up to date.");
                emit updateAvailable(DatasetManager::UpdateStatus::UpToDate, "New Version");
                emit readyToUse();
            } else {
                emit statusChanged("Downloading and processing dataset...");
                emit updateAvailable(DatasetManager::UpdateStatus::UpToDate, "New Version");
                startDownloadAndImport();
            }
        } else {
            if (QFile::exists(Config::instance().getDatasetPath())) {
                emit statusChanged("Server unreachable. Operating in offline mode.");
                emit updateAvailable(DatasetManager::UpdateStatus::Error, "New Version");
                emit readyToUse();
            } else {
                emit statusChanged("Failed to check updates and no local database found.");
                emit updateAvailable(DatasetManager::UpdateStatus::Error, "New Version");
            }
        }
        reply->deleteLater();
    });
}

int DatasetManager::getLocalRowCount() {
    if (!QFile::exists(Config::instance().getDatasetPath())) return 0;

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "info_connection");
    db.setDatabaseName(Config::instance().getDatasetPath());
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
    QFileInfo info(Config::instance().getDatasetPath());
    if (!info.exists()) return 0.0;
    return info.size() / (1024.0 * 1024.0);
}
