#include "CardImageProvider.h"
#include "../database/DatabaseUtil.h"

#include <QNetworkRequest>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPixmapCache>
#include <QTimer>

QString CardImageProvider::cacheDir() {
    QString d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/cards";
    QDir().mkpath(d);
    return d;
}

QString CardImageProvider::cacheFilePath(const QString& url) {
    QByteArray h = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    return cacheDir() + "/" + QString::fromLatin1(h) + ".img";
}

static QNetworkAccessManager& sharedNam() {
    thread_local QNetworkAccessManager nam;
    return nam;
}

CardImageResponse::CardImageResponse(const QString &cardCode,
                                     const QSize &requestedSize,
                                     DatabaseUtil *dbUtil)
    : requestedSize_(requestedSize)
    , cardCode_(cardCode)
    , dbUtil_(dbUtil)
{
    const QString url = dbUtil ? dbUtil->imageUrlFor(cardCode) : QString();

    aliasPath_ = CardImageProvider::cacheFilePath("fallback:" + (url.isEmpty() ? cardCode : url));

    if (QFile::exists(aliasPath_)) {
        QImage img(aliasPath_);
        if (!img.isNull()) {
            image_ = img;
            QTimer::singleShot(0, this, [this] { emit finished(); });
            return;
        }
    }

    if (url.isEmpty()) {
        tryOfficialOrFinish();
        return;
    }

    const QString path = CardImageProvider::cacheFilePath(url);
    if (QFile::exists(path)) {
        QImage img(path);
        if (!img.isNull()) {
            image_ = img;
            QTimer::singleShot(0, this, [this] { emit finished(); });
            return;
        }
    }

    qDebug() << "[CandImageProvider] api call to: " << url;
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "WSDeckImporter/1.0");
    reply_ = sharedNam().get(req);
    cachePath_ = path;
    connect(reply_, &QNetworkReply::finished, this, &CardImageResponse::onFinished);
}

void CardImageResponse::tryOfficialOrFinish()
{
    if (triedOfficial_ || !dbUtil_ || cardCode_.isEmpty()) {
        QTimer::singleShot(0, this, [this] { emit finished(); });
        return;
    }
    triedOfficial_ = true;

    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(dbUtil_,
                    &DatabaseUtil::officialImageUrlReady,
                    this,
                    [this, conn](const QString &code, const QString &url) {
                        if (code != cardCode_)
                            return;
                        QObject::disconnect(*conn);

                        if (url.isEmpty()) {
                            emit finished();
                            return;
                        }

                        cachePath_ = CardImageProvider::cacheFilePath(url);
                        if (QFile::exists(cachePath_)) {
                            QImage img(cachePath_);
                            if (!img.isNull()) {
                                image_ = img;
                                if (!aliasPath_.isEmpty() && !QFile::exists(aliasPath_))
                                    QFile::copy(cachePath_, aliasPath_);
                                emit finished();
                                return;
                            }
                        }

                        QNetworkRequest req{QUrl(url)};
                        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                         QNetworkRequest::NoLessSafeRedirectPolicy);
                        req.setHeader(QNetworkRequest::UserAgentHeader, "WSDeckImporter/1.0");
                        reply_ = sharedNam().get(req);
                        connect(reply_,
                                &QNetworkReply::finished,
                                this,
                                &CardImageResponse::onFinishedOfficial);
                    });

    dbUtil_->fetchOfficialImageUrl(cardCode_);
}

void CardImageResponse::onFinishedOfficial()
{
    if (reply_->error() == QNetworkReply::NoError) {
        const QByteArray bytes = reply_->readAll();
        QImage img;
        if (img.loadFromData(bytes)) {
            image_ = img;
            for (const QString &p : {cachePath_, aliasPath_}) {
                if (p.isEmpty())
                    continue;
                QFile f(p);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    f.write(bytes);
            }
        }
    }
    reply_->deleteLater();
    reply_ = nullptr;
    emit finished();
}

void CardImageResponse::cancel()
{
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
        reply_->deleteLater();
        reply_ = nullptr;
    }
}

void CardImageResponse::onFinished()
{
    bool loaded = false;
    if (reply_->error() == QNetworkReply::NoError) {
        const QByteArray bytes = reply_->readAll();
        QImage img;
        if (img.loadFromData(bytes)) {
            image_ = img;
            QFile f(cachePath_);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(bytes);
            loaded = true;
        }
    }
    reply_->deleteLater();
    reply_ = nullptr;

    if (loaded) {
        emit finished();
        return;
    }

    tryOfficialOrFinish();
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