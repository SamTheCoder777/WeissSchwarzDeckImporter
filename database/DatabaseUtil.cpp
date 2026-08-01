#include "databaseutil.h"
#include "../Config.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

QString DatabaseUtil::imageUrlFor(const QString &cardId) const{
    QSqlDatabase db = QSqlDatabase::database("main_ui_connection");

    if (!db.isOpen()) {
        qDebug() << "DatabaseUtil::imageUrlFor - Main UI Database is not open!";
        return QString();
    }

    const QString baseImgUrl = Config::instance().getBaseImgUrl();
    QSqlQuery query(db);

    if (!query.prepare("SELECT \"picture\" FROM dataset WHERE \"card_number\" = ?")) {
        qDebug() << "DatabaseUtil::imageUrlFor - Prepare failed:" << query.lastError().text();
        return QString();
    }

    query.addBindValue(cardId);

    if (query.exec()) {
        if (query.next()) {
            return baseImgUrl + query.value(0).toString();
        }
    } else {
        qDebug() << "DatabaseUtil::imageUrlFor - Query failed:" << query.lastError().text();
    }

    return QString();
}

QVariantMap DatabaseUtil::cardDataFor(const QString &cardId) const{
    QSqlDatabase db = QSqlDatabase::database("main_ui_connection");
    QVariantMap card;

    if (!db.isOpen()) {
        qDebug() << "DatabaseUtil::cardDataFor - Main UI Database is not open!";
        return card;
    }

    const QString baseImgUrl = Config::instance().getBaseImgUrl();
    QSqlQuery query(db);

    if (!query.prepare("SELECT \"picture\", \"set_name\", \"rare\", \"feature1\", \"power\", \"soul\","
                       " \"flavor\", \"color\", \"text\", \"feature2\", \"card_name\", \"card_trigger\", \"card_kind\", "
                       "\"level\" FROM dataset WHERE \"card_number\" = ?")) {
        qDebug() << "DatabaseUtil::imageUrlFor - Prepare failed:" << query.lastError().text();
        return card;
    }

    query.addBindValue(cardId);

    if (query.exec()) {
        if (query.next()) {
            card["picture"]      = baseImgUrl+query.value("picture").toString();
            card["setName"]      = query.value("set_name").toString();
            card["rarity"]       = query.value("rare").toString();
            card["feature1"]     = query.value("feature1").toString();
            card["power"]        = query.value("power").toString();
            card["soul"]         = query.value("soul").toString();
            card["flavor"]       = query.value("flavor").toString();
            card["color"]        = query.value("color").toString();
            card["text"]         = query.value("text").toString();
            card["feature2"]     = query.value("feature2").toString();
            card["cardName"]     = query.value("card_name").toString();
            card["cardTrigger"]  = query.value("card_trigger").toString();
            card["cardKind"]     = query.value("card_kind").toString();
            card["level"]        = query.value("level").toString();
            //qDebug() << "DatabaseUtil::cardDataFor - Query:" << card["picture"];
        } else{
            qDebug() << "DatabaseUtil::cardDataFor - Query failed: query next not possible for cardId: "<<cardId;
        }
    } else {
        qDebug() << "DatabaseUtil::cardDataFor - Query failed:" << query.lastError().text();
    }

    return card;
}
