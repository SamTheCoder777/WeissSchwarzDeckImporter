#include "DatabaseUtil.h"
#include "../core/Config.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>


static const char* kCardConn = "cardlist_catalog_connection";

void DatabaseUtil::setLocale(const QString& loc) {
    QString v = (loc.compare("JP", Qt::CaseInsensitive) == 0) ? "JP" : "EN";
    if (v == locale_) return;
    locale_ = v;
    Config::instance().setPreferredLocale(v);
    emit localeChanged();
}


static QJsonObject fetchCardObject(const QString& cardCode, bool& ok) {
    ok = false;
    QSqlDatabase db = QSqlDatabase::database(kCardConn);
    if (!db.isOpen()) {
        qDebug() << "DatabaseUtil - cardList.db not open";
        return {};
    }
    QSqlQuery q(db);
    q.prepare("SELECT data FROM cards WHERE cardcode = ?");
    q.addBindValue(cardCode);
    if (!q.exec()) { qDebug() << "DatabaseUtil query failed:" << q.lastError().text(); return {}; }
    if (!q.next()) return {};
    QJsonObject o = QJsonDocument::fromJson(q.value(0).toByteArray()).object();
    ok = true;
    return o;
}

QString DatabaseUtil::imageUrlFor(const QString &cardCode) const {
    bool ok = false;
    QJsonObject o = fetchCardObject(cardCode, ok);
    if (!ok) return {};
    const QString path = o.value("imagepath").toString();
    if (path.isEmpty()) return {};
    return Config::instance().getImgUrl(path);
}

QVariantMap DatabaseUtil::cardDataFor(const QString &cardCode) const {
    QVariantMap card;
    bool ok = false;
    QJsonObject o = fetchCardObject(cardCode, ok);
    if (!ok) return card;

    card["cardId"]      = o.value("cardcode").toString();
    card["cardCode"]    = o.value("cardcode").toString();
    card["setName"]     = o.value("set").toString();
    card["rarity"]      = o.value("rarity").toString();
    card["color"]       = o.value("colour").toString();
    card["cardKind"]    = o.value("cardtype").toString();
    card["cardType"]    = o.value("cardtype").toString();

    auto numOrEmpty = [&](const char* k)->QString{
        QJsonValue v = o.value(k);
        return v.isDouble() ? QString::number(v.toInt()) : v.toString();
    };
    card["power"]       = numOrEmpty("power");
    card["soul"]        = numOrEmpty("soul");
    card["level"]       = numOrEmpty("level");
    card["cost"]        = numOrEmpty("cost");

    QStringList trig;
    for (const QJsonValue& t : o.value("trigger").toArray()) trig << t.toString();
    card["cardTrigger"] = trig.join(", ");

    card["picture"]     = Config::instance().getImgUrl(o.value("imagepath").toString());

    auto blockKeyFor = [](const QString& userLoc)->QString{
        return (userLoc == "JP") ? "NP" : "EN";
    };
    auto localeBlock = [&](const QString& userLoc)->QJsonObject{
        return o.value(blockKeyFor(userLoc)).toObject();
    };
    auto blockHasContent = [](const QJsonObject& b){
        return !b.value("name").toString().isEmpty()
        || !b.value("ability").toArray().isEmpty();
    };
    QJsonObject blk = localeBlock(locale_);
    QString usedLocale = locale_;
    if (!blockHasContent(blk)) {
        QString other = (locale_ == "EN") ? "JP" : "EN";
        QJsonObject alt = localeBlock(other);
        if (blockHasContent(alt)) { blk = alt; usedLocale = other; }
    }
    card["locale"] = usedLocale;

    QString name = blk.value("name").toString();
    if (name.isEmpty()) name = o.value("name").toString();
    card["cardName"] = name;

    QStringList lines;
    for (const QJsonValue& a : blk.value("ability").toArray()) lines << a.toString();
    card["text"] = lines.join("\n\n");

    card["flavor"] = blk.value("flavor").toString();

    QStringList attrs;
    for (const QJsonValue& a : blk.value("attributes").toArray()) attrs << a.toString();
    card["feature1"] = attrs.value(0);
    card["feature2"] = attrs.value(1);
    card["features"] = attrs.join(" / ");

    return card;
}