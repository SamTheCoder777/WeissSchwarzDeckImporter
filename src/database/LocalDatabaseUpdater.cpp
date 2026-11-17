#include "LocalDatabaseUpdater.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryFile>
#include <QUuid>
#include <QVariant>
#include <QtConcurrent>

#include "../core/Config.h"
#include "../network/NetGuard.h"
#include "miniz.h"

namespace {
constexpr qint64 MaxZipBytes = 64LL * 1024 * 1024;
constexpr qint64 MaxDatabaseBytes = 512LL * 1024 * 1024;
constexpr qint64 CheckIntervalSeconds = 3 * 60 * 60;

bool validUrl(const QUrl &url)
{
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty()
           && url.userInfo().isEmpty() && !url.hasFragment();
}

size_t extractChunk(void *opaque, mz_uint64 offset, const void *data, size_t size)
{
    auto *file = static_cast<QFile *>(opaque);
    if (offset != mz_uint64(file->pos()) || offset > mz_uint64(MaxDatabaseBytes)
        || size > mz_uint64(MaxDatabaseBytes) - offset)
        return 0;
    const qint64 written = file->write(static_cast<const char *>(data), qint64(size));
    return written == qint64(size) ? size : 0;
}

QString validateDatabase(const QString &path)
{
    const QString connection = "local_db_validation_"
                               + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString error;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path);
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (!db.open()) {
            error = "The downloaded database could not be opened: " + db.lastError().text();
        } else {
            {
                QSqlQuery query(db);
                if (!query.exec(
                        "SELECT global_id,card_number,set_edition,title_number,set_name,expansion,"
                        "card_name,card_name_kana,card_kind,color,level,cost,power,soul,card_"
                        "trigger,"
                        "rare,feature1,feature2,feature3,text,translated_text,flavor,picture "
                        "FROM cards LIMIT 0")) {
                    error = "The downloaded database has an incompatible cards table: "
                            + query.lastError().text();
                }
                query.finish();
                if (error.isEmpty()) {
                    if (!query.exec("PRAGMA quick_check(1)") || !query.next()
                        || query.value(0).toString() != "ok") {
                        error = "The downloaded database failed SQLite validation.";
                    }
                }
                query.finish();
                if (error.isEmpty()) {
                    if (!query.exec("PRAGMA journal_mode") || !query.next()
                        || query.value(0).toString().compare("delete", Qt::CaseInsensitive) != 0) {
                        error = "Publish a standalone SQLite database in DELETE journal mode.";
                    }
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return error;
}

QString unpackAndInstall(const QString &zipPath, const QString &target)
{
    QFile archive(zipPath);
    if (!archive.open(QIODevice::ReadOnly) || archive.size() < 1 || archive.size() > MaxZipBytes)
        return "The downloaded ZIP could not be read.";
    const QByteArray bytes = archive.readAll();
    if (bytes.size() != archive.size())
        return "Could not read the complete ZIP.";
    archive.close();

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.constData(), size_t(bytes.size()), 0))
        return "The download is not a supported ZIP archive.";
    struct ZipCloser
    {
        mz_zip_archive *zip;
        ~ZipCloser() { mz_zip_reader_end(zip); }
    } closer{&zip};

    mz_zip_archive_file_stat entry{};
    if (mz_zip_reader_get_num_files(&zip) != 1 || !mz_zip_reader_file_stat(&zip, 0, &entry)
        || mz_zip_reader_is_file_a_directory(&zip, 0))
        return "ws-db.zip must contain exactly one SQLite file at the archive root.";
    const QString name = QString::fromUtf8(entry.m_filename);
    if (name.contains('/') || name.contains('\\') || name.contains(':')
        || !name.endsWith(".sqlite", Qt::CaseInsensitive) || entry.m_uncomp_size < 16
        || entry.m_uncomp_size > mz_uint64(MaxDatabaseBytes))
        return "The ZIP must contain one root-level .sqlite file, no larger than 512 MiB.";

    QTemporaryFile staged(QDir::tempPath() + "/ws-local-db-XXXXXX.sqlite");
    if (!staged.open())
        return "Could not create the temporary database: " + staged.errorString();
    if (!mz_zip_reader_extract_to_callback(&zip, 0, extractChunk, &staged, 0)
        || staged.size() != qint64(entry.m_uncomp_size) || !staged.flush())
        return "Could not extract the database, or the ZIP is corrupt.";
    if (!staged.seek(0) || staged.read(16) != QByteArray("SQLite format 3\0", 16))
        return "The extracted file is not an SQLite database.";
    staged.close();

    const QString validation = validateDatabase(staged.fileName());
    if (!validation.isEmpty())
        return validation;

    // This updater runs before this process opens the installed database.
    QLockFile lock(target + ".update.lock");
    if (!lock.tryLock(0))
        return "Could not lock the database for updating. Close other app instances "
               "and check write access to the install folder.";
    for (const QString &suffix : {QString("-wal"), QString("-shm"), QString("-journal")}) {
        if (QFileInfo::exists(target + suffix))
            return "Database sidecar files are present. Close other copies of the app; "
                   "the existing database has not been replaced.";
    }

    QFile source(staged.fileName());
    if (!source.open(QIODevice::ReadOnly))
        return "Could not reopen the extracted database.";
    QSaveFile installed(target);
    installed.setDirectWriteFallback(false);
    if (!installed.open(QIODevice::WriteOnly))
        return "Could not write the database beside the EXE: " + installed.errorString();
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(64 * 1024);
        if (chunk.isEmpty() || installed.write(chunk) != chunk.size())
            return "Could not write the complete database. The old file was retained.";
    }
    if (!installed.commit())
        return "Could not replace the local database: " + installed.errorString();
    return {};
}
} // namespace

LocalDatabaseUpdater::LocalDatabaseUpdater(QObject *parent)
    : QObject(parent)
{
    downloadDeadline_.setSingleShot(true);
    connect(&downloadDeadline_, &QTimer::timeout, this, [this] {
        if (reply_) {
            downloadError_ = "The database download exceeded its time limit.";
            reply_->abort();
        }
    });
    connect(&installer_, &QFutureWatcher<QString>::finished, this, [this] {
        const QString error = installer_.result();
        if (error.isEmpty()) {
            auto &config = Config::instance();
            config.setLocalDatabaseHash(expectedHash_);
            config.setLocalDatabaseUpdatePending(false);
        }
        finish(error);
    });
}

LocalDatabaseUpdater::~LocalDatabaseUpdater()
{
    if (manifestRequest_)
        NetGuard::instance().cancel(manifestRequest_);
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
    }
    // Keep the ZIP alive until the worker has finished using it.
    if (installer_.isRunning())
        installer_.waitForFinished();
}

QString LocalDatabaseUpdater::destination()
{
    return QCoreApplication::applicationDirPath() + "/weiss-db.sqlite";
}

bool LocalDatabaseUpdater::checkDue()
{
    if (Config::instance().getLocalDatabaseUpdatePending())
        return true;

    const QFileInfo file(destination());
    if (!file.isFile() || file.size() == 0)
        return true;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 last = Config::instance().getLocalDatabaseLastCheckUtc();
    return last <= 0 || last > now || now - last >= CheckIntervalSeconds;
}

void LocalDatabaseUpdater::start()
{
    if (!checkDue()) {
        finish();
        return;
    }
    auto &config = Config::instance();
    const QUrl manifestUrl(config.localDatabaseVersionUrl_);
    downloadUrl_ = QUrl(config.localDatabaseDownloadUrl_);
    if (!validUrl(manifestUrl) || !validUrl(downloadUrl_)) {
        finish("Set valid HTTPS database URLs in Config.h.");
        return;
    }
    config.setLocalDatabaseLastCheckUtc(QDateTime::currentSecsSinceEpoch());
    emit progress("Checking local card database…");
    NetRequest request;
    request.url = manifestUrl;
    request.key = "local-database-version:" + manifestUrl.toString();
    request.useCache = false;
    request.timeoutMs = 15000;
    request.retries = 0;
    manifestRequest_ = NetGuard::instance().get(request, this, [this](const NetResult &result) {
        manifestRequest_ = 0;
        if (!result.ok()) {
            finish("Could not check the local database: " + result.message);
            return;
        }
        if (result.body.size() > 64 * 1024) {
            finish("The database version JSON is too large.");
            return;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(result.body, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            finish("The database version JSON is invalid.");
            return;
        }
        expectedHash_ = document.object().value("hash").toString().trimmed().toLower();
        static const QRegularExpression sha256("^[0-9a-f]{64}$");
        if (!sha256.match(expectedHash_).hasMatch()) {
            finish("version.json must contain a 64-character SHA-256 hash.");
            return;
        }
        const QFileInfo file(destination());
        if (file.isFile() && file.size() > 0
            && expectedHash_ == Config::instance().getLocalDatabaseHash().toLower()) {
            Config::instance().setLocalDatabaseUpdatePending(false);
            finish();
            return;
        }
        download();
    });
}

void LocalDatabaseUpdater::download()
{
    auto &guard = NetGuard::instance();
    if (guard.blockedForMs(downloadUrl_) > 0) {
        finish("The database download host is in cooldown. The existing database was retained.");
        return;
    }
    zip_ = std::make_unique<QTemporaryFile>(QDir::tempPath() + "/ws-local-db-XXXXXX.zip");
    if (!zip_->open()) {
        finish("Could not create a temporary ZIP: " + zip_->errorString());
        return;
    }
    zipHash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    emit progress("Downloading local card database…");
    QNetworkRequest request(downloadUrl_);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, "WSDeckImporter/1.0");
    request.setTransferTimeout(30000);
    reply_ = guard.nam()->get(request);
    reply_->setReadBufferSize(256 * 1024);
    connect(reply_, &QNetworkReply::readyRead, this, &LocalDatabaseUpdater::consumeDownload);
    connect(reply_, &QNetworkReply::finished, this, [this] {
        downloadDeadline_.stop();
        consumeDownload();
        QNetworkReply *reply = reply_;
        reply_ = nullptr;
        QString error = downloadError_;
        if (error.isEmpty()) {
            const NetResult verdict = NetGuard::instance().noteReply(downloadUrl_, reply);
            if (!verdict.ok())
                error = "Could not download the database: " + verdict.message;
        }
        reply->deleteLater();
        if (!error.isEmpty()) {
            finish(error);
            return;
        }
        if (!zip_->flush()) {
            finish("Could not save the downloaded ZIP.");
            return;
        }
        
        zip_->close();
        const QString zipPath = zip_->fileName(), target = destination();
        emit progress("Installing local card database…");
        installer_.setFuture(
            QtConcurrent::run([zipPath, target] { return unpackAndInstall(zipPath, target); }));
    });
    downloadDeadline_.start(10 * 60 * 1000);
}

void LocalDatabaseUpdater::consumeDownload()
{
    if (!reply_ || !downloadError_.isEmpty())
        return;
    while (reply_->bytesAvailable() > 0) {
        const QByteArray chunk = reply_->read(64 * 1024);
        if (chunk.isEmpty())
            break;
        if (chunk.size() > MaxZipBytes - zip_->size()) {
            downloadError_ = "The database ZIP exceeds 64 MiB.";
            reply_->abort();
            return;
        }
        if (zip_->write(chunk) != chunk.size()) {
            downloadError_ = "Could not write the temporary ZIP.";
            reply_->abort();
            return;
        }
        zipHash_->addData(chunk);
    }
}

void LocalDatabaseUpdater::finish(const QString &error)
{
    if (done_)
        return;
    done_ = true;
    downloadDeadline_.stop();
    if (zip_) {
        zip_->close();
        if (QFileInfo::exists(zip_->fileName()) && !zip_->remove())
            qWarning() << "[LocalDB] Could not remove temporary ZIP:" << zip_->fileName();
        zip_.reset();
    }
    zipHash_.reset();
    emit finished(error);
}

void LocalDatabaseUpdater::runBeforeMainWindow()
{
    if (!checkDue())
        return;
    const bool previousQuitPolicy = qApp->quitOnLastWindowClosed();
    qApp->setQuitOnLastWindowClosed(false);
    LocalDatabaseUpdater updater;
    QEventLoop loop;
    QProgressDialog dialog;
    dialog.setWindowTitle("Local card database");
    dialog.setCancelButton(nullptr);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setRange(0, 0);
    dialog.setLabelText("Checking local card database…");
    dialog.setMinimumDuration(0);
    QString error;
    connect(&updater, &LocalDatabaseUpdater::progress, &dialog, &QProgressDialog::setLabelText);
    connect(&updater, &LocalDatabaseUpdater::finished, &loop, [&](const QString &message) {
        error = message;
        loop.quit();
    });
    dialog.show();
    QTimer::singleShot(0, &updater, [&updater] { updater.start(); });
    loop.exec();
    dialog.close();
    if (!error.isEmpty()) {
        QMessageBox::warning(
            nullptr,
            "Local database update",
            error + "\n\nThe app will continue with the existing database, if available.");
    }
    qApp->setQuitOnLastWindowClosed(previousQuitPolicy);
}
