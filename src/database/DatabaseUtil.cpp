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

static qint64 intervalToSeconds(int interval)
{
    switch ((Config::MissingPurgeInterval) interval) {
    case Config::MissingPurgeInterval::Hourly:
        return 3600;
    case Config::MissingPurgeInterval::Daily:
        return 86400;
    case Config::MissingPurgeInterval::Weekly:
        return 7LL * 86400;
    case Config::MissingPurgeInterval::Monthly:
        return 30LL * 86400;
    case Config::MissingPurgeInterval::Never:
        return 0;
    }
    return 0;
}

static const char* kCardConn = "cardlist_catalog_connection";

static QSqlDatabase getCardDb()
{
    const QString conn = QStringLiteral("cardlist_conn_%1")
                             .arg((quintptr) QThread::currentThreadId());
    QSqlDatabase db;
    if (QSqlDatabase::contains(conn)) {
        db = QSqlDatabase::database(conn);
    } else {
        db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(Config::instance().getCardListDatabasePath());
    }
    if (!db.isOpen() && !db.open()) {
        qDebug() << "DatabaseUtil - cardList.db open failed";
        return QSqlDatabase();
    }
    return db;
}

static QJsonObject fetchCardObject(const QString& cardCode, bool& ok) {
    ok = false;

    const QString conn = QStringLiteral("cardlist_conn_%1")
                             .arg((quintptr)QThread::currentThreadId());

    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return {};

    QSqlQuery q(db);
    q.prepare("SELECT data FROM cards WHERE LOWER(cardcode) = ?");
    q.addBindValue(cardCode.toLower());
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
        const QString path = o.value("imagepath").toString();
        if (!path.isEmpty()) {
            QString url = isOfficial
                              ? OfficialFallback::imageBaseUrl() + path
                              : Config::instance().getImgUrl(path);
            return url;
        }
    }

    return QString();
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

    QJsonObject selBlk = localeBlock(locale_);
    QString otherLoc = (locale_ == "EN") ? "JP" : "EN";
    QJsonObject otherBlk = localeBlock(otherLoc);

    QJsonObject blk;
    bool anyAvailable = true;
    if (blockHasContent(selBlk)) {
        blk = selBlk;
        card["locale"] = locale_;
    } else if (blockHasContent(otherBlk)) {
        blk = otherBlk;
        card["locale"] = otherLoc;
    } else {
        anyAvailable = false;
    }
    card["localeAvailable"] = anyAvailable;

    if (!anyAvailable) {
        card["cardName"] = "";
        card["text"]     = "";
        card["flavor"]   = "";
        card["feature1"] = "";
        card["feature2"] = "";
        card["features"] = "";
        card["source"]   = o.value("_source").toString().isEmpty()
                             ? "encoredecks" : o.value("_source").toString();
        return card;
    }

    QString name = blk.value("name").toString();
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

    card["source"] = o.value("_source").toString().isEmpty()
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

    if (isKnownMissing(cardCode)) {
        emit cardFetchFailed(cardCode, "No card data available");
        return;
    }

    auto it = fetchState_.find(cardCode);
    if (it != fetchState_.end()) {
        if (it->permanent) {emit cardFetchFailed(cardCode, "No card data available"); return;}
        if (QDateTime::currentMSecsSinceEpoch() < it->nextRetryMs){
            emit cardFetchFailed(cardCode, "Rate limited — try again later");
            return;
        }
    }

    const QUrl url(OfficialFallback::dataUrlFromCardcode(cardCode));
    qDebug() << "[DatabaseUtil] api call to: " << url;
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TCGDeckBuilder/1.0");
    QNetworkReply* reply = nam_.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cardCode] {
        reply->deleteLater();

        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            auto& st = fetchState_[cardCode];
            st.failures++;

            if (st.failures >= kMaxRetries) {
                st.permanent = true;
                emit cardFetchFailed(cardCode, "Couldn't load...");
                markMissing(cardCode, "Couldn't load");
                return;
            }

            qint64 base = (http == 429) ? 60000 : 5000;
            qint64 backoff = base * (1 << qMin(st.failures - 1, 4));
            st.nextRetryMs = QDateTime::currentMSecsSinceEpoch() + backoff;
            qDebug() << "fetch failed" << cardCode << "http" << http
                     << "retry in" << backoff << "ms";

            const QString reason = (http == 429) ? "Rate limited — try again later"
                                                : "Network error";
            emit cardFetchFailed(cardCode, reason);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray items = doc.object().value("items").toArray();
        if (items.isEmpty()) {
            fetchState_[cardCode].permanent = true;
            qDebug() << "official: no items for" << cardCode;
            emit cardFetchFailed(cardCode, "No card data available");
            markMissing(cardCode, "No card data available");
            return;
        }
        QJsonObject item = items.first().toObject();
        for (const QJsonValue& v : items) {
            if (v.toObject().value("card_number").toString() == cardCode) {
                item = v.toObject(); break;
            }
        }
        QJsonObject shaped = OfficialFallback::reshapeOfficialItem(item);
        storeOfficialCard(cardCode, shaped);
        fetchState_.remove(cardCode);
        emit cardReady(cardCode);
    });
}

void DatabaseUtil::ensureMissingTable()
{
    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return;
    QSqlQuery q(db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS missing_cards ("
                "cardcode TEXT PRIMARY KEY, "
                "reason TEXT, "
                "checked_at INTEGER)")) {
        qDebug() << "ensureMissingTable failed:" << q.lastError().text();
    }
}

bool DatabaseUtil::isKnownMissing(const QString &cardCode) const
{
    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return false;

    const qint64 lifetimeSecs = intervalToSeconds(Config::instance().getMissingPurgeInterval());

    QSqlQuery q(db);
    if (lifetimeSecs == 0) {
        q.prepare("SELECT 1 FROM missing_cards WHERE cardcode = ?");
        q.addBindValue(cardCode);
    } else {
        const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - lifetimeSecs;
        q.prepare("SELECT 1 FROM missing_cards WHERE cardcode = ? AND checked_at >= ?");
        q.addBindValue(cardCode);
        q.addBindValue(cutoff);
    }
    if (!q.exec())
        return false;
    return q.next();
}

void DatabaseUtil::markMissing(const QString &cardCode, const QString &reason)
{
    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return;

    ensureMissingTable();

    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO missing_cards (cardcode, reason, checked_at) "
              "VALUES (?, ?, ?)");
    q.addBindValue(cardCode);
    q.addBindValue(reason);
    q.addBindValue((qint64) QDateTime::currentSecsSinceEpoch());
    if (!q.exec())
        qDebug() << "markMissing failed:" << q.lastError().text();
    else
        qDebug() << "marked missing:" << cardCode << "(" << reason << ")";
}

void DatabaseUtil::purgeMissingCards()
{
    QSqlDatabase db = getCardDb();
    if (!db.isValid()) {
        emit missingCardsPurged(0);
        return;
    }

    QSqlQuery q(db);
    int count = 0;
    if (q.exec("SELECT COUNT(*) FROM missing_cards") && q.next())
        count = q.value(0).toInt();

    if (!q.exec("DELETE FROM missing_cards"))
        qDebug() << "purgeMissingCards failed:" << q.lastError().text();
    else
        qDebug() << "purged" << count << "missing-card records";

    emit missingCardsPurged(count);
}

void DatabaseUtil::cleanupExpiredMissing()
{
    const qint64 lifetime = intervalToSeconds(Config::instance().getMissingPurgeInterval());
    if (lifetime == 0)
        return;
    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return;
    const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - lifetime;
    QSqlQuery q(db);
    q.prepare("DELETE FROM missing_cards WHERE checked_at < ?");
    q.addBindValue(cutoff);
    q.exec();
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