#pragma once
#include <QJsonObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariantMap>

namespace LocalCardStore {
void close(const QString &path);
QSqlDatabase open(const QString &path, QString *error = nullptr);

QVariantMap card(const QString &path,
                 const QString &edition,
                 const QString &code,
                 const QString &language);

QVariantList search(const QString &path, const QString &edition, const QString &query, int limit);

int count(const QString &path, const QString &edition, const QVariantMap &filters);

QStringList codes(const QString &path, const QString &edition, const QVariantMap &filters);
QStringList traits(const QString &path, const QString &edition, const QStringList &sets);
QStringList triggers(const QString &path, const QString &edition, const QStringList &sets);
QString officialImage(const QString &picture, const QString &edition);
QString kind(QString value);
QStringList icons(QString value);
QString trigger(QString value);
QString plainText(QString value);

// deck specific
QVariantMap deckTitles(const QString &path, const QString &edition);
QVariantMap deckPage(const QString &path,
                     const QString &edition,
                     const QString &titleNumber,
                     const QVariantMap &filters,
                     const QString &language,
                     int offset,
                     int limit);
QVariantMap deckGroups(const QString &path, const QString &edition, const QJsonObject &manifest);

void releaseThreadConnection(const QString &path);

} // namespace LocalCardStore
