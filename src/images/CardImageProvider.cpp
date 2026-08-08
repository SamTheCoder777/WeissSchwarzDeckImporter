#include "CardImageProvider.h"
#include "../database/DatabaseUtil.h"

#include <QNetworkRequest>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPixmapCache>

QString CardImageProvider::cacheDir() {
    QString d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/cards";
    QDir().mkpath(d);
    return d;
}

QString CardImageProvider::cacheFilePath(const QString& url) {
    QByteArray h = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    return cacheDir() + "/" + QString::fromLatin1(h) + ".img";
}

CardImageResponse::CardImageResponse(const QString& cardCode, const QSize& requestedSize,
                                     DatabaseUtil* dbUtil, QNetworkAccessManager* nam)
    : requestedSize_(requestedSize) {

    const QString url = dbUtil ? dbUtil->imageUrlFor(cardCode) : QString();
    if (url.isEmpty()) { emit finished(); return; }

    const QString path = CardImageProvider::cacheFilePath(url);
    if (QFile::exists(path)) {
        QImage img(path);
        if (!img.isNull()) { image_ = img; emit finished(); return; }
    }

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TCGDeckBuilder/1.0");
    reply_ = nam->get(req);
    cachePath_ = path;
    connect(reply_, &QNetworkReply::finished, this, &CardImageResponse::onFinished);
}

QQuickTextureFactory* CardImageResponse::textureFactory() const {
    return QQuickTextureFactory::textureFactoryForImage(image_);
}

void CardImageResponse::onFinished() {
    if (reply_->error() == QNetworkReply::NoError) {
        const QByteArray bytes = reply_->readAll();
        QImage img;
        if (img.loadFromData(bytes)) {
            image_ = img;
            QFile f(cachePath_);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(bytes);
        }
    }
    reply_->deleteLater();
    reply_ = nullptr;
    emit finished();
}

CardImageProvider::CardImageProvider(DatabaseUtil* dbUtil)
    : dbUtil_(dbUtil) {}

QQuickImageResponse* CardImageProvider::requestImageResponse(const QString& id,
                                                             const QSize& requestedSize) {
    const QString cardCode = QUrl::fromPercentEncoding(id.toUtf8());
    return new CardImageResponse(cardCode, requestedSize, dbUtil_, &nam_);
}