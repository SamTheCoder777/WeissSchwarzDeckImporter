#pragma once

#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QSize>
#include <QString>
#include <QStringList>

class DatabaseUtil;

class CardImageResponse : public QQuickImageResponse {
    Q_OBJECT
public:
    CardImageResponse(const QString& cardCode, const QSize& requestedSize,
                      DatabaseUtil* dbUtil);

    QQuickTextureFactory* textureFactory() const override;

private:
    DatabaseUtil *dbUtil_ = nullptr;

    QSize requestedSize_;
    QImage image_;
    quint64 requestId_ = 0;
    QString cachePath_;
    QString cardCode_;

    QStringList urls_;
    int nextUrl_ = 0;
    bool completed_ = false;

    void tryNextImage();
    void complete();

    void cancel() override;
};

class CardImageProvider : public QQuickAsyncImageProvider {
public:
    explicit CardImageProvider(DatabaseUtil* dbUtil);

    QQuickImageResponse* requestImageResponse(const QString& id,
                                              const QSize& requestedSize) override;

    static QString cacheDir();
    static QString cacheFilePath(const QString &url);
    static bool clearImageCaches();
    static QImage memoryImage(const QString &cardCode);
    static void rememberImage(const QString &cardCode, const QImage &image);

private:
    DatabaseUtil *dbUtil_;
};