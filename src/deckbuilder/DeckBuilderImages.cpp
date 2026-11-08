#include "DeckBuilderImages.h"
#include <utility>
#include "../database/LocalCardStore.h"
#include "../images/CardImageProvider.h"
#include "../network/NetGuard.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDebug>
#include <QImageReader>
#include <QMetaObject>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

namespace {
QImage decode(const QByteArray &bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 20000000)
        return {};
    return reader.read();
}
}

QString DeckBuilderImages::source(const QString &officialUrl)
{
    if (officialUrl.isEmpty())
        return {};
    return QStringLiteral("image://deckcards/")
           + QString::fromLatin1(QUrl::toPercentEncoding(officialUrl));
}

quint64 DeckBuilderImages::load(const QString &officialUrl, QObject *context, Callback callback)
{
    // Main-thread entry point. Full JP and EN URLs are both validated by officialImage().
    const QString url = LocalCardStore::officialImage(officialUrl, QStringLiteral("jp"));
    if (url.isEmpty()) {
        QTimer::singleShot(0, context, [callback] { callback({}, "No valid official image URL."); });
        return 0;
    }
    const QString path = CardImageProvider::cacheFilePath(url);
    QImage cached(path);
    if (!cached.isNull()) {
        QTimer::singleShot(0, context, [callback, cached] { callback(cached, {}); });
        return 0;
    }
    NetRequest request;
    request.url = QUrl(url);
    request.key = QStringLiteral("card-image:") + url;
    request.useCache = false;
    request.timeoutMs = 15000;
    request.maxBodyBytes = 15LL * 1024 * 1024;
    request.retries = 0;
    return NetGuard::instance().get(request, context,
        [callback, path](const NetResult &result) {
            if (!result.ok()) {
                callback({}, result.message);
                return;
            }
            const QImage image = decode(result.body);
            if (image.isNull()) {
                callback({}, "The server did not return a valid card image.");
                return;
            }
            QSaveFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                if (file.write(result.body) == result.body.size()) {
                    if (!file.commit())
                        qWarning() << "[DeckBuilder] Image cache commit failed:" << path;
                } else {
                    file.cancelWriting();
                }
            }
            callback(image, {});
        });
}

namespace {
class Response final : public QQuickImageResponse {
public:
    Response(QString url, QSize size) : url_(std::move(url)), size_(size)
    {
        moveToThread(QCoreApplication::instance()->thread());
        QMetaObject::invokeMethod(this, [this] {
            if (done_)
                return;
            id_ = DeckBuilderImages::load(url_, this,
                [this](const QImage &image, const QString &) {
                    id_ = 0;
                    if (done_)
                        return;
                    image_ = image;
                    finish();
                });
        }, Qt::QueuedConnection);
    }
    QQuickTextureFactory *textureFactory() const override
    {
        QImage image = image_;
        if (!image.isNull() && size_.width() > 0 && size_.height() > 0)
            image = image.scaled(size_, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return QQuickTextureFactory::textureFactoryForImage(image);
    }
    void cancel() override
    {
        QMetaObject::invokeMethod(this, [this] {
            if (done_)
                return;
            const quint64 id = id_;
            id_ = 0;
            if (id)
                NetGuard::instance().cancel(id);
            finish();
        }, Qt::QueuedConnection);
    }
private:
    void finish() { if (!done_) { done_ = true; emit finished(); } }
    QString url_;
    QSize size_;
    QImage image_;
    quint64 id_ = 0;
    bool done_ = false;
};
}

QQuickImageResponse *DeckBuilderImageProvider::requestImageResponse(
    const QString &id, const QSize &requestedSize)
{
    return new Response(QUrl::fromPercentEncoding(id.toUtf8()), requestedSize);
}
