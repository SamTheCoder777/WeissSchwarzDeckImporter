#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>

class OfficialFallback {
public:
    static QString imageUrlFromCardcode(const QString& cardcode);

    static QString dataUrlFromCardcode(const QString& cardcode);

    static QJsonObject reshapeOfficialItem(const QJsonObject& item);
    static const QString imageBaseUrl(){
        return "https://ws-tcg.com/wordpress/wp-content/images/cardlist/";
    }
};
