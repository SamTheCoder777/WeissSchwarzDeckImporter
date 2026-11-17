#include "CardImageProvider.h"

#include <QCache>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPixmapCache>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

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
static QString cardImageNamespace;

static QString imageMemoryKey(const QString &cardCode)
{
    return cardImageNamespace + QChar(0x1f) + cardCode.trimmed().toUpper();
}

void CardImageProvider::setCacheNamespace(const QString &value)
{
    QMutexLocker lock(&cardImageMutex);

    if (value == cardImageNamespace)
        return;

    cardImageNamespace = value;
    cardImages.clear();
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
                                     DatabaseUtil *dbUtil,
                                     const QString &expectedStamp)
    : dbUtil_(dbUtil)
    , requestedSize_(requestedSize)
    , cardCode_(cardCode)
    , expectedStamp_(expectedStamp)
{
    moveToThread(QCoreApplication::instance()->thread());

    QMetaObject::invokeMethod(
        this,
        [this] {
            if (completed_)
                return;

            if (!dbUtil_) {
                complete();
                return;
            }

            dataRevision_ = dbUtil_->dataRevision();
            dataContext_ = dbUtil_->imageContext();

            const QUrl currentUrl(dbUtil_->cardImageSource(cardCode_));

            const QString actualStamp = QUrlQuery(currentUrl).queryItemValue("context");

            if (!expectedStamp_.isEmpty() && expectedStamp_ != actualStamp) {
                complete();
                return;
            }

            CardImageProvider::setCacheNamespace(dataContext_ + QChar(0x1f)
                                                 + QString::number(dataRevision_));

            image_ = CardImageProvider::memoryImage(cardCode_);

            if (!image_.isNull()) {
                complete();
                return;
            }

            urls_ = dbUtil_->imageUrlsFor(cardCode_);
            tryNextImage();
        },
        Qt::QueuedConnection);
}

bool CardImageResponse::currentDataContext() const
{
    return dbUtil_ && dataRevision_ == dbUtil_->dataRevision()
           && dataContext_ == dbUtil_->imageContext();
}

void CardImageResponse::complete()
{
    if (completed_)
        return;

    completed_ = true;

    if (!currentDataContext()) {
        image_ = QImage();
    } else if (!image_.isNull()) {
        CardImageProvider::rememberImage(cardCode_, image_);
    }

    emit finished();
}

void CardImageResponse::tryNextImage()
{
    if (completed_)
        return;

    if (!currentDataContext()) {
        complete();
        return;
    }

    while (nextUrl_ < urls_.size()) {
        const QString url = urls_.at(nextUrl_++);

        const QString path = CardImageProvider::cacheFilePath(url);

        QImage cached(path);

        if (!cached.isNull()) {
            image_ = cached;

            dbUtil_->reportLoadedImage(cardCode_, url);

            complete();
            return;
        }

        if (dbUtil_->imageRetryBlocked(url))
            continue;

        NetRequest request;
        request.url = QUrl(url);
        request.key = QStringLiteral("card-image:") + url;
        request.useCache = false;
        request.timeoutMs = 15000;
        request.maxBodyBytes = 15LL * 1024 * 1024;
        request.retries = 0;

        requestId_ = NetGuard::instance()
                         .get(request, this, [this, url, path](const NetResult &result) {
                             requestId_ = 0;

                             if (completed_)
                                 return;

                             if (!currentDataContext() || result.status == NetResult::Cancelled) {
                                 complete();
                                 return;
                             }

                             QImage downloaded;

                             if (result.ok())
                                 downloaded.loadFromData(result.body);

                             if (!downloaded.isNull()) {
                                 image_ = downloaded;

                                 dbUtil_->reportLoadedImage(cardCode_, url);

                                 QSaveFile file(path);

                                 if (file.open(QIODevice::WriteOnly)) {
                                     if (file.write(result.body) == result.body.size()) {
                                         if (!file.commit()) {
                                             qWarning() << "[IMAGE] Cache commit failed:" << path;
                                         }
                                     } else {
                                         file.cancelWriting();
                                     }
                                 }

                                 complete();
                                 return;
                             }

                             if (!result.blocked && result.status == NetResult::ClientError) {
                                 dbUtil_->recordImageFailure(url, result.http);
                             }

                             qWarning() << "[IMAGE]" << url << result.message;

                             tryNextImage();
                         });

        return;
    }

    complete();
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

QQuickImageResponse *CardImageProvider::requestImageResponse(const QString &id,
                                                             const QSize &requestedSize)
{
    const int separator = id.indexOf('?');
    const QString encodedCode = separator < 0 ? id : id.left(separator);
    const QString query = separator < 0 ? QString() : id.mid(separator + 1);
    const QString cardCode = QUrl::fromPercentEncoding(encodedCode.toUtf8());
    const QString expectedStamp = QUrlQuery(query).queryItemValue("context");

    return new CardImageResponse(cardCode, requestedSize, dbUtil_, expectedStamp);
}