#pragma
#include <QString>
#include <QSqlDatabase>

namespace DatabaseUtil
{
    QString imageUrlFor(const QSqlDatabase &db, const QString &cardId);
};

