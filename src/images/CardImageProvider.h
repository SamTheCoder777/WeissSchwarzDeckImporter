#pragma once

#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImage>
#include <QString>
#include <QSize>

class DatabaseUtil;

class CardImageResponse : public QQuickImageResponse {
    Q_OBJECT
public:
    CardImageResponse(const QString& cardCode, const QSize& requestedSize,
                      DatabaseUtil* dbUtil);

    QQuickTextureFactory* textureFactory() const override;

private slots:
    void onFinished();

private:
    QSize          requestedSize_;
    QImage         image_;
    QNetworkReply* reply_ = nullptr;
    QString        cachePath_;
};

class CardImageProvider : public QQuickAsyncImageProvider {
public:
    explicit CardImageProvider(DatabaseUtil* dbUtil);

    QQuickImageResponse* requestImageResponse(const QString& id,
                                              const QSize& requestedSize) override;

    static QString cacheDir();
    static QString cacheFilePath(const QString& url);

private:
    DatabaseUtil*         dbUtil_;
};