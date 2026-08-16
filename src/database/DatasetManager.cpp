#include "DatasetManager.h"
#include "../core/Config.h"

#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QSqlQuery>
#include <QSqlError>

DatasetManager::DatasetManager(const DatabaseWorker::DatabaseMode mode, QObject *parent):
    curMode_(mode), QObject(parent){}

QSqlDatabase DatasetManager::getUiDatabase() {
    QString connName;

    switch (curMode_) {
        case DatabaseWorker::DatabaseMode::cardList:
            connName = "seriesList_ui_connection";
            break;
        case DatabaseWorker::DatabaseMode::seriesList:
            connName = "cardList_ui_connection";
            break;
    }

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
    db.setDatabaseName(Config::instance().getSeriesListDatabasePath());

    if (!db.open()) {
        qCritical() << "Failed to open UI database:" << db.lastError().text();
    }

    return db;
}


void DatasetManager::startDownloadAndImport() {
    if (isDownloading_) return;

    isDownloading_ = true;
    qRegisterMetaType<QByteArrayList>("QByteArrayList");

    worker_ = new DatabaseWorker();
    worker_->setMode(curMode_);
    worker_->moveToThread(&workerThread_);

    // connect worker's status
    connect(worker_, &DatabaseWorker::statusChanged, this, &DatasetManager::statusChanged);

    connect(&workerThread_, &QThread::started, worker_, [this]() {
        switch (curMode_) {
            case DatabaseWorker::DatabaseMode::cardList:
                QMetaObject::invokeMethod(worker_, "initCardListDatabase", Q_ARG(bool, false));
                break;
            case DatabaseWorker::DatabaseMode::seriesList:
                QMetaObject::invokeMethod(worker_, "initSerieslistDatabase", Q_ARG(bool, false));
                break;
        }
    });

    connect(worker_, &DatabaseWorker::finished, &workerThread_, &QThread::quit);
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);

    connect(worker_, &DatabaseWorker::finished, this, [this]() {
        isDownloading_ = false;
        switch (curMode_) {
            case DatabaseWorker::DatabaseMode::cardList:{
                    QRegularExpressionMatch match = seriesRegex_.match(datasetUrl_.toString());
                    if (match.hasMatch())
                        Config::instance().setCardListEtag(match.captured(1), remoteEtag_);
                    break;
            }
            case DatabaseWorker::DatabaseMode::seriesList:
                Config::instance().setJpSeriestListEtag(remoteEtag_);
                break;
        }
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

        if (bytesReceived/bytesTotal == 1){
            qDebug() << "Finished downloading! Now processing dataset...";
            emit statusChanged("Finished downloading! Now processing dataset...");
        }
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

        QString dbPath;

        switch (curMode_) {
            case DatabaseWorker::DatabaseMode::cardList:
                dbPath = Config::instance().getCardListDatabasePath();
                break;
            case DatabaseWorker::DatabaseMode::seriesList:
                dbPath = Config::instance().getSeriesListDatabasePath();
                break;
        }

        if (reply->error() == QNetworkReply::NoError) {
            remoteEtag_ = reply->rawHeader("ETag");

            QString cachedEtag;
            bool dbExists;

            switch (curMode_) {
                case DatabaseWorker::DatabaseMode::cardList:{
                    QRegularExpressionMatch match = seriesRegex_.match(datasetUrl_.toString());
                    if (match.hasMatch()){
                        QString series = match.captured(1);
                        cachedEtag = Config::instance().getCardListEtag(series);
                    }
                        dbExists = QFile::exists(Config::instance().getCardListDatabasePath())
                                        && QFileInfo(Config::instance().getCardListDatabasePath()).size() != 0;
                        break;
                }
                case DatabaseWorker::DatabaseMode::seriesList:
                    cachedEtag = Config::instance().getJpSeriesListEtag();
                    dbExists = QFile::exists(Config::instance().getSeriesListDatabasePath())
                               && QFileInfo(Config::instance().getSeriesListDatabasePath()).size() != 0;
                    break;
            }

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
            if (QFile::exists(dbPath)) {
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
        QString dbPath;

        switch (curMode_) {
            case DatabaseWorker::DatabaseMode::cardList:
                dbPath = Config::instance().getCardListDatabasePath();
                break;
            case DatabaseWorker::DatabaseMode::seriesList:
                dbPath = Config::instance().getSeriesListDatabasePath();
                break;
        }

        if (reply->error() == QNetworkReply::NoError) {
            remoteEtag_ = reply->rawHeader("ETag");

            QString cachedEtag;
            bool dbExists;

            switch (curMode_) {
                case DatabaseWorker::DatabaseMode::cardList: {
                        QRegularExpressionMatch match = seriesRegex_.match(datasetUrl_.toString());
                        if (match.hasMatch()){
                            QString series = match.captured(1);
                            cachedEtag = Config::instance().getCardListEtag(series);
                        }
                        dbExists = QFile::exists(Config::instance().getCardListDatabasePath())
                                   && QFileInfo(Config::instance().getCardListDatabasePath()).size() != 0;
                        break;
            }
                case DatabaseWorker::DatabaseMode::seriesList:
                    cachedEtag = Config::instance().getJpSeriesListEtag();
                    dbExists = QFile::exists(Config::instance().getSeriesListDatabasePath())
                               && QFileInfo(Config::instance().getSeriesListDatabasePath()).size() != 0;
                    break;
            }

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
            if (QFile::exists(dbPath)) {
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
    QString dbPath;

    switch (curMode_) {
        case DatabaseWorker::DatabaseMode::cardList:
            dbPath = Config::instance().getCardListDatabasePath();
            break;
        case DatabaseWorker::DatabaseMode::seriesList:
            dbPath = Config::instance().getSeriesListDatabasePath();
            break;
    }

    if (!QFile::exists(dbPath)) return 0;

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "info_connection");
    db.setDatabaseName(dbPath);
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
    QString dbPath;

    switch (curMode_) {
        case DatabaseWorker::DatabaseMode::cardList:
            dbPath = Config::instance().getCardListDatabasePath();
            break;
        case DatabaseWorker::DatabaseMode::seriesList:
            dbPath = Config::instance().getSeriesListDatabasePath();
            break;
    }

    QFileInfo info(dbPath);
    if (!info.exists()) return 0.0;
    return info.size() / (1024.0 * 1024.0);
}

void DatasetManager::resetDatabase()
{
    if (isDownloading_) return;

    emit statusChanged("Resetting database…");

    QString dbPath;
    QString tableName;
    switch (curMode_) {
    case DatabaseWorker::DatabaseMode::cardList:
        dbPath    = Config::instance().getCardListDatabasePath();
        tableName = "cards";
        break;
    case DatabaseWorker::DatabaseMode::seriesList:
        dbPath    = Config::instance().getSeriesListDatabasePath();
        tableName = "series";
        break;
    }

    {
        const QString conn = "reset_connection";
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery q(db);
            if (!q.exec("DROP TABLE IF EXISTS " + tableName))
                qWarning() << "reset drop failed:" << q.lastError().text();
            db.close();
        } else {
            qWarning() << "reset: could not open" << dbPath << db.lastError().text();
        }
    }
    QSqlDatabase::removeDatabase("reset_connection");

    switch (curMode_) {
    case DatabaseWorker::DatabaseMode::cardList:
        Config::instance().clearCardListEtags();
        break;
    case DatabaseWorker::DatabaseMode::seriesList:
        Config::instance().setJpSeriestListEtag("");
        break;
    }

    emit statusChanged("Database reset. Re-download to repopulate.");
    emit updateAvailable(DatasetManager::UpdateStatus::Error, "New Version");
    emit readyToUse();
}
