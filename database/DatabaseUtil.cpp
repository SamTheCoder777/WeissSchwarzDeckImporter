#include "databaseutil.h"
#include "../Config.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace DatabaseUtil {

QString imageUrlFor(const QSqlDatabase &db, const QString &cardId) {
    if (!db.isOpen()) {
        qDebug() << "DatabaseUtil::imageUrlFor - Database is not open!";
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

}