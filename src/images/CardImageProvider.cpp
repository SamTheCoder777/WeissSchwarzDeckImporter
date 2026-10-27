#include "CardImageProvider.h"

#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QPixmapCache>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include "../core/Config.h"
#include "../database/DatabaseUtil.h"
#include "../network/NetGuard.h"

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
    moveToThread(QCoreApplication::instance()->thread());

    QMetaObject::invokeMethod(
        this,
        [this] {
            if (completed_)
                return;

            image_ = CardImageProvider::memoryImage(cardCode_);

            if (!image_.isNull()) {
                complete();
                return;
            }

            if (dbUtil_)
                urls_ = dbUtil_->imageUrlsFor(cardCode_);

            tryNextImage();
        },
        Qt::QueuedConnection);
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
        const QString path = CardImageProvider::cacheFilePath(url);

        // 1. Existing disk cache.
        QImage cached(path);
        if (!cached.isNull()) {
            image_ = cached;

            if (dbUtil_)
                dbUtil_->reportLoadedImage(cardCode_, url);

            complete();
            return;
        }

        // 2. Previously confirmed missing URL.
        if (dbUtil_ && dbUtil_->imageRetryBlocked(url))
            continue;

        // 3. Network request through NetGuard.
        NetRequest request;
        request.url = QUrl(url);
        request.key = QStringLiteral("card-image:") + url;
        request.useCache = false;
        request.timeoutMs = 15000;
        request.retries = 0; // Start conservatively.

        requestId_ = NetGuard::instance()
                         .get(request, this, [this, url, path](const NetResult &result) {
                             requestId_ = 0;

                             if (completed_)
                                 return;

                             if (result.status == NetResult::Cancelled) {
                                 complete();
                                 return;
                             }

                             QImage downloaded;
                             if (result.ok())
                                 downloaded.loadFromData(result.body);

                             if (!downloaded.isNull()) {
                                 image_ = downloaded;

                                 if (dbUtil_)
                                     dbUtil_->reportLoadedImage(cardCode_, url);

                                 // Atomic replacement of the image cache file.
                                 QSaveFile file(path);
                                 if (file.open(QIODevice::WriteOnly)) {
                                     if (file.write(result.body) == result.body.size()) {
                                         if (!file.commit())
                                             qWarning() << "Image cache commit failed:" << path;
                                     } else {
                                         file.cancelWriting();
                                         qWarning() << "Image cache write failed:" << path;
                                     }
                                 }

                                 complete(); // Also populates your memory cache.
                                 return;
                             }

                             // Temporary failures must not become missing-image records.
                             if (!result.blocked && result.status == NetResult::ClientError
                                 && (result.http == 404 || result.http == 410) && dbUtil_) {
                                 dbUtil_->recordImageFailure(url, result.http);
                             }

                             qWarning() << "[IMAGE]" << url
                                        << (result.ok() ? QStringLiteral("Invalid image data")
                                                        : result.message);

                             // Cached alternatives can still load.
                             // NetGuard rejects network requests to a blocked host.
                             tryNextImage();
                         });

        return; // Continue when the callback completes.
    }

    complete(); // No usable image.
}

void CardImageResponse::cancel()
{
    QMetaObject::invokeMethod(
        this,
        [this] {
            if (completed_)
                return;

            const quint64 id = requestId_;
            requestId_ = 0;

            if (id)
                NetGuard::instance().cancel(id);

            complete();
        },
        Qt::QueuedConnection);
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