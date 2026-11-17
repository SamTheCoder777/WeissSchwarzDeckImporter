#pragma once
#include <QImage>
#include <QObject>
#include <QQuickAsyncImageProvider>
#include <functional>

namespace DeckBuilderImages {
using Callback = std::function<void(const QImage &, const QString &)>;
QString source(const QString &officialUrl);
quint64 load(const QString &officialUrl, QObject *context, Callback callback);
}

class DeckBuilderImageProvider final : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;
};
