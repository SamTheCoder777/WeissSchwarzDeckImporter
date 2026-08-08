#include "DatabaseUtil.h"

#include "../core/Config.h"
#include "../database/OfficialFallback.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QThread>

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QThread>


static const char* kCardConn = "cardlist_catalog_connection";

static QJsonObject fetchCardObject(const QString& cardCode, bool& ok) {
    ok = false;

    const QString conn = QStringLiteral("cardlist_conn_%1")
                             .arg((quintptr)QThread::currentThreadId());

    QSqlDatabase db;
    if (QSqlDatabase::contains(conn)) {
        db = QSqlDatabase::database(conn);
    } else {
        db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(Config::instance().getCardListDatabasePath());
    }
    if (!db.isOpen() && !db.open()) {
        qDebug() << "DatabaseUtil - cardList.db open failed";
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

void DatabaseUtil::setLocale(const QString& loc) {
    QString v = (loc.compare("JP", Qt::CaseInsensitive) == 0) ? "JP" : "EN";
    if (v == locale_) return;
    locale_ = v;
    Config::instance().setPreferredLocale(v);
    emit localeChanged();
}

QString DatabaseUtil::imageUrlFor(const QString &cardCode) const {
    bool ok = false;
    QJsonObject o = fetchCardObject(cardCode, ok);
    if (ok) {
        const bool isOfficial = o.value("_source").toString() == "official";
        if (isOfficial) {
            return OfficialFallback::imageUrlFromCardcode(cardCode);
        }
        const QString path = o.value("imagepath").toString();
        if (!path.isEmpty()) return Config::instance().getImgUrl(path);
    }
    //qDebug() << OfficialFallback::imageUrlFromCardcode(cardCode);

    // Not in encore decks, fall back to official api
    return OfficialFallback::imageUrlFromCardcode(cardCode);
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
        QJsonObject localeRoot = o.value("locale").toObject();
        return localeRoot.value(blockKeyFor(userLoc)).toObject();
    };
    auto blockHasContent = [](const QJsonObject& b){
        return !b.value("name").toString().isEmpty()
        || !b.value("ability").toArray().isEmpty();
    };
    QJsonObject blk = localeBlock(locale_);
    const bool available = blockHasContent(blk);
    card["locale"] = locale_;
    card["localeAvailable"] = available;

    if (!available) {
        card["cardName"] = "";
        card["text"]     = "";
        card["flavor"]   = "";
        card["feature1"] = "";
        card["feature2"] = "";
        return card;
    }

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

    card["localeAvailable"] = o.value("_localeAvailable").toBool(true);
    card["source"]          = o.value("_source").toString().isEmpty()
                         ? "encoredecks" : o.value("_source").toString();

    return card;
}

bool DatabaseUtil::cardInDb(const QString& cardCode) const {
    bool ok = false;
    fetchCardObject(cardCode, ok);
    return ok;
}

void DatabaseUtil::ensureCardData(const QString& cardCode) {

    {
        bool ok = false;
        QJsonObject existing = fetchCardObject(cardCode, ok);
        if (ok) {
            QJsonObject loc = existing.value("locale").toObject();
            const bool hasName =
                !loc.value("NP").toObject().value("name").toString().isEmpty() ||
                !loc.value("EN").toObject().value("name").toString().isEmpty();
            if (hasName) { emit cardReady(cardCode); return; }
        }
    }

    const QUrl url(OfficialFallback::dataUrlFromCardcode(cardCode));
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TCGDeckBuilder/1.0");
    QNetworkReply* reply = nam_.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cardCode] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "official fetch failed for" << cardCode << reply->errorString();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray items = doc.object().value("items").toArray();
        if (items.isEmpty()) { qDebug() << "official: no items for" << cardCode; return; }
        QJsonObject item = items.first().toObject();
        for (const QJsonValue& v : items) {
            if (v.toObject().value("card_number").toString() == cardCode) {
                item = v.toObject(); break;
            }
        }
        QJsonObject shaped = OfficialFallback::reshapeOfficialItem(item);
        storeOfficialCard(cardCode, shaped);
        emit cardReady(cardCode);
    });
}
void DatabaseUtil::storeOfficialCard(const QString& cardCode, const QJsonObject& shaped) {
    const QString conn = QStringLiteral("cardlist_conn_%1")
                             .arg((quintptr)QThread::currentThreadId());
    QSqlDatabase db = QSqlDatabase::contains(conn)
                          ? QSqlDatabase::database(conn)
                          : QSqlDatabase::addDatabase("QSQLITE", conn);
    if (!db.isOpen()) {
        db.setDatabaseName(Config::instance().getCardListDatabasePath());
        db.open();
    }
    QSqlQuery ins(db);
    ins.prepare("INSERT OR REPLACE INTO cards (series_id, card_id, cardcode, data) "
                "VALUES (?,?,?,?)");
    ins.addBindValue("__official__");
    ins.addBindValue(cardCode);
    ins.addBindValue(cardCode);
    ins.addBindValue(QString::fromUtf8(
        QJsonDocument(shaped).toJson(QJsonDocument::Compact)));
    if (!ins.exec())
        qDebug() << "storeOfficialCard failed:" << ins.lastError().text();
}