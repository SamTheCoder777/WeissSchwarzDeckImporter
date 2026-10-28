#pragma once

#include <QJsonObject>
#include <QString>

class CardResolver
{
public:
    static QString imageUrl(const QJsonObject &card);
};