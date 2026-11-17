#pragma once
#include <QString>
#include <QStringList>
#include <QHash>

namespace TriggerAliases {
inline QString canonical(QString value)
{
    value = value.trimmed().toUpper();
    value.replace("\\.", ".");
    value.remove('[').remove(']');
    if (value.endsWith(".GIF")) value.chop(4);
    value = value.trimmed();
    static const QHash<QString, QString> aliases{{"BOOK", "DRAW"}, {"STOCK", "POOL"},
        {"BAG", "POOL"}, {"SALVAGE", "COMEBACK"}, {"BOUNCE", "RETURN"},
        {"PANTS", "GATE"}, {"BAR", "TREASURE"}};
    return aliases.value(value, value);
}
inline QStringList terms(const QString &value)
{
    const QString key = canonical(value);
    if (key == "DRAW") return {"DRAW", "BOOK"};
    if (key == "RETURN") return {"RETURN", "BOUNCE"};
    if (key == "COMEBACK") return {"COMEBACK", "SALVAGE"};
    if (key == "POOL") return {"POOL", "STOCK", "BAG"};
    if (key == "GATE") return {"GATE", "PANTS"};
    if (key == "TREASURE") return {"TREASURE", "BAR"};
    return key.isEmpty() ? QStringList{} : QStringList{key};
}
} // namespace TriggerAliases
