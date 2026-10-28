#include "CardResolver.h"
#include "../core/Config.h"

#include <QUrl>

QString CardResolver::imageUrl(const QJsonObject &card)
{
    // Reject previously stored official records before processing URLs
    if (card.value("_source").toString().compare("official", Qt::CaseInsensitive) == 0) {
        return {};
    }

    const QString path = card.value("imagepath").toString().trimmed();
    if (path.isEmpty())
        return {};

    const QUrl supplied(path);

    const QString resolved = supplied.isRelative() ? Config::instance().getImgUrl(path) : path;

    const QUrl url(resolved);

    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http")) {
        return {};
    }

    // Also reject official URLs in records with missing/incorrect source tags.
    const QString host = url.host().toLower();

    if (host == "ws-tcg.com" || host.endsWith(".ws-tcg.com")) {
        return {};
    }

    return resolved;
}