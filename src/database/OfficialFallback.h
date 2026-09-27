#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class OfficialFallback {
public:
    static QString dataUrlFromCardcode(const QString& cardcode);

    static QJsonObject reshapeOfficialItem(const QJsonObject& item);
    static const QString imageBaseUrl(){
        return "https://ws-tcg.com/wordpress/wp-content/images/cardlist/";
    }

    // English
    static QString enSiteBaseUrl() { return "https://en.ws-tcg.com"; }
    static QString enPageUrlFromCardcode(const QString &cardcode);
    static QJsonObject reshapeEnOfficialHtml(const QByteArray &html,
                                             const QString &expectedCardcode);

    // Shared
    static QString resolveImageUrl(const QJsonObject &card);
};
