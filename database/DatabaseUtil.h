#pragma once
#include <QString>
#include <QSqlDatabase>

class DatabaseUtil : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    struct CardData{
        QString cardId;
        QString setName;
        QString rarity;
        QString feature1;
        QString feature2;
        QString power;
        QString picture;
        QString soul;
        QString flavor;
        QString color;
        QString text;
        QString cost;
        QString level;
        QString cardName;
        QString cardTrigger;
    };

    Q_INVOKABLE QString imageUrlFor(const QString &cardId) const;

    Q_INVOKABLE QVariantMap cardDataFor(const QString &cardId) const;
};

