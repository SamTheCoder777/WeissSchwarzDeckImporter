#include "CardImageProvider.h"

#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkRequest>
#include <QPixmapCache>
#include <QStandardPaths>
#include <QTimer>

#include "../core/Config.h"
#include "../database/DatabaseUtil.h"

QString CardImageProvider::cacheDir() {
    QString d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/cards";
    QDir().mkpath(d);
    return d;
}

QString CardImageProvider::cacheFilePath(const QString& url) {
    QByteArray h = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    return cacheDir() + "/" + QString::fromLatin1(h) + ".img";
}

static QMutex cardImageMutex;
static QCache<QString, QImage> cardImages(64 * 1024);

static QString imageMemoryKey(const QString &cardCode)
{
    return Config::instance().getCardListDatabasePath() + QChar(0x1f)
           + cardCode.trimmed().toUpper();
}

bool CardImageProvider::clearImageCaches()
{
    {
        QMutexLocker lock(&cardImageMutex);
        cardImages.clear();
    }

    QDir dir(cacheDir());
    bool success = true;

    const QStringList files = dir.entryList(QStringList{"*.img"}, QDir::Files);

    for (const QString &file : files) {
        if (!dir.remove(file)) {
            qWarning() << "[IMAGE][PURGE] Could not remove:" << dir.filePath(file);
            success = false;
        }
    }

    qDebug() << "[IMAGE][PURGE] Memory cleared; disk success:" << success;

    return success;
}

static QNetworkAccessManager& sharedNam() {
    thread_local QNetworkAccessManager nam;
    return nam;
}

QImage CardImageProvider::memoryImage(const QString &cardCode)
{
    QMutexLocker lock(&cardImageMutex);

    if (const QImage *image = cardImages.object(imageMemoryKey(cardCode))) {
        return *image;
    }

    return {};
}

void CardImageProvider::rememberImage(const QString &cardCode, const QImage &image)
{
    if (image.isNull())
        return;

    const qint64 bytes = qint64(image.bytesPerLine()) * image.height();

    // file size safety check
    if (bytes > 64LL * 1024 * 1024)
        return;

    const int cost = int((bytes + 1023) / 1024);

    QMutexLocker lock(&cardImageMutex);

    cardImages.insert(imageMemoryKey(cardCode), new QImage(image), qMax(1, cost));
}

CardImageResponse::CardImageResponse(const QString &cardCode,
                                     const QSize &requestedSize,
                                     DatabaseUtil *dbUtil)
    : requestedSize_(requestedSize)
    , cardCode_(cardCode)
    , dbUtil_(dbUtil)
{
    image_ = CardImageProvider::memoryImage(cardCode);

    if (!image_.isNull()) {
        qDebug() << "[IMAGE][QML][MEMORY]" << cardCode;

        QTimer::singleShot(0, this, [this] { complete(); });
        return;
    }

    if (dbUtil)
        urls_ = dbUtil->imageUrlsFor(cardCode);

    QTimer::singleShot(0, this, [this] { tryNextImage(); });
}

void CardImageResponse::complete()
{
    if (completed_)
        return;

    completed_ = true;

    if (!image_.isNull())
        CardImageProvider::rememberImage(cardCode_, image_);

    emit finished();
}

void CardImageResponse::tryNextImage()
{
    if (completed_)
        return;

    while (nextUrl_ < urls_.size()) {
        const QString url = urls_.at(nextUrl_++);
        cachePath_ = CardImageProvider::cacheFilePath(url);

        QImage cached(cachePath_);

        if (!cached.isNull()) {
            qDebug() << "[IMAGE][QML][DISK]" << cardCode_ << url;

            image_ = cached;

            if (dbUtil_)
                dbUtil_->reportLoadedImage(cardCode_, url);

            complete();
            return;
        }

        if (dbUtil_ && dbUtil_->imageRetryBlocked(url)) {
            qDebug() << "[IMAGE][SKIP MISSING DB]" << url;
            continue;
        }

        QNetworkRequest request{QUrl(url)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, "WSDeckImporter/1.0");

        qDebug() << "[IMAGE][QML][HTTP GET]" << cardCode_ << request.url();
        reply_ = sharedNam().get(request);

        connect(reply_, &QNetworkReply::finished, this, &CardImageResponse::onFinished);
        return;
    }

    complete();
}

void CardImageResponse::cancel()
{
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }

    QTimer::singleShot(0, this, [this] { complete(); });
}

void CardImageResponse::onFinished()
{
    QNetworkReply *reply = reply_;
    if (!reply)
        return;

    reply_ = nullptr;

    QByteArray bytes;
    QImage downloaded;

    if (reply->error() == QNetworkReply::NoError) {
        bytes = reply->readAll();
        downloaded.loadFromData(bytes);
    }

    const QString attemptedUrl = reply->request().url().toString();

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (downloaded.isNull()) {
        qDebug() << "[IMAGE][QML][FAILED]" << cardCode_ << attemptedUrl << "HTTP:" << httpStatus
                 << "Network error:" << int(reply->error()) << reply->errorString();

        if (dbUtil_)
            dbUtil_->recordImageFailure(attemptedUrl, httpStatus);
    }

    reply->deleteLater();

    if (!downloaded.isNull()) {
        image_ = downloaded;

        qDebug() << "[IMAGE][QML][DOWNLOADED]" << cardCode_;

        if (dbUtil_)
            dbUtil_->reportLoadedImage(cardCode_, attemptedUrl);

        QFile file(cachePath_);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(bytes);

        complete();
        return;
    }

    tryNextImage();
}

QQuickTextureFactory* CardImageResponse::textureFactory() const {
    return QQuickTextureFactory::textureFactoryForImage(image_);
}

CardImageProvider::CardImageProvider(DatabaseUtil* dbUtil)
    : dbUtil_(dbUtil) {}

QQuickImageResponse* CardImageProvider::requestImageResponse(const QString& id,
                                                             const QSize& requestedSize) {
    QString clean = id;
    int q = clean.indexOf('?');
    if (q >= 0) clean = clean.left(q);
    const QString cardCode = QUrl::fromPercentEncoding(clean.toUtf8());
    return new CardImageResponse(cardCode, requestedSize, dbUtil_);
}