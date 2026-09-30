#include "DatabaseUtil.h"

#include "../core/Config.h"
#include "../database/OfficialFallback.h"

#include <QDateTime>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
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

static bool isEnMode()
{
    return Config::instance().getCurDetectLocaleMode() == Config::DetectLocaleMode::EN;
}

static QString likeContains(QString s)
{
    s.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");
    return "%" + s + "%";
}

static bool buildAdvancedWhere(const QVariantMap &f, QString &where, QVariantList &binds)
{
    QStringList conds;
    auto str = [&](const char *k) { return f.value(k).toString().trimmed(); };

    auto bothLocales = [&](const char *field, const QString &v) {
        conds << QString("(json_extract(data,'$.locale.EN.%1') LIKE ? ESCAPE '\\' "
                         "OR json_extract(data,'$.locale.NP.%1') LIKE ? ESCAPE '\\')")
                     .arg(field);
        binds << likeContains(v) << likeContains(v);
    };
    if (const QString v = str("name"); !v.isEmpty())
        bothLocales("name", v);
    if (const QString v = str("text"); !v.isEmpty())
        bothLocales("ability", v);
    if (const QString v = str("trait"); !v.isEmpty()) {
        conds << "(EXISTS (SELECT 1 FROM json_each(json_extract(data,'$.locale.EN.attributes')) "
                 "         WHERE value = ? COLLATE NOCASE) "
                 " OR EXISTS (SELECT 1 FROM json_each(json_extract(data,'$.locale.NP.attributes')) "
                 "         WHERE value = ? COLLATE NOCASE))";
        binds << v << v;
    }

    if (const QString v = str("code"); !v.isEmpty()) {
        conds << "cardcode LIKE ? ESCAPE '\\'";
        binds << likeContains(v);
    }
    if (const QString v = str("rarity"); !v.isEmpty()) {
        conds << "UPPER(json_extract(data,'$.rarity')) = ?";
        binds << v.toUpper();
    }
    if (const QString v = str("trigger"); !v.isEmpty()) {
        if (v.compare("none", Qt::CaseInsensitive) == 0) {
            conds << "COALESCE(json_array_length(json_extract(data,'$.trigger')), 0) = 0";
        } else {
            conds << "UPPER(json_extract(data,'$.trigger')) LIKE ? ESCAPE '\\'";
            binds << likeContains(v.toUpper());
        }
    }
    if (const QString v = str("cardType"); !v.isEmpty()) {
        conds << "json_extract(data,'$.cardtype') = ?";
        binds << v;
    }

    const QStringList colors = f.value("colors").toStringList();
    if (!colors.isEmpty()) {
        QStringList marks;
        for (const QString &c : colors) {
            marks << "?";
            binds << c.toUpper();
        }
        conds << QString("UPPER(json_extract(data,'$.colour')) IN (%1)").arg(marks.join(','));
    }

    auto range = [&](const char *stat, const char *key, const char *op) {
        bool ok = false;
        const int n = f.value(key).toString().trimmed().toInt(&ok);
        if (!ok)
            return;
        conds << QString("CAST(json_extract(data,'$.%1') AS INTEGER) %2 ?").arg(stat, op);
        binds << n;
    };
    range("level", "levelMin", ">=");
    range("level", "levelMax", "<=");
    range("cost", "costMin", ">=");
    range("cost", "costMax", "<=");
    range("power", "powerMin", ">=");
    range("power", "powerMax", "<=");
    range("soul", "soulMin", ">=");
    range("soul", "soulMax", "<=");

    where = conds.join(" AND ");
    return !conds.isEmpty();
}

static QSqlDatabase getCardDb()
{
    const QString path = Config::instance().getCardListDatabasePath();
    const QString conn = QStringLiteral("cardlist_conn_%1")
                             .arg((quintptr) QThread::currentThreadId());

    QSqlDatabase db = QSqlDatabase::contains(conn) ? QSqlDatabase::database(conn, false)
                                                   : QSqlDatabase::addDatabase("QSQLITE", conn);

    if (db.databaseName() != path) {
        if (db.isOpen())
            db.close();
        db.setDatabaseName(path);
    }
    if (!db.isOpen() && !db.open()) {
        qDebug() << "DatabaseUtil -" << path << "open failed:" << db.lastError().text();
        return QSqlDatabase();
    }
    return db;
}

static QJsonObject fetchCardObject(const QString &cardCode, bool &ok)
{
    ok = false;

    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return {};

    QSqlQuery q(db);
    q.prepare("SELECT data FROM cards WHERE LOWER(cardcode) = ?");
    q.addBindValue(cardCode.toLower());
    if (!q.exec()) {
        qDebug() << "DatabaseUtil query failed:" << q.lastError().text();
        return {};
    }
    if (!q.next())
        return {};
    QJsonObject o = QJsonDocument::fromJson(q.value(0).toByteArray()).object();
    ok = true;
    return o;
}

static QJsonObject parseJpOfficial(const QByteArray &body, const QString &cardCode)
{
    const QJsonArray items = QJsonDocument::fromJson(body).object().value("items").toArray();
    if (items.isEmpty())
        return {};
    QJsonObject item = items.first().toObject();
    for (const QJsonValue &v : items) {
        if (v.toObject().value("card_number").toString() == cardCode) {
            item = v.toObject();
            break;
        }
    }
    return OfficialFallback::reshapeOfficialItem(item);
}

void DatabaseUtil::setLocale(const QString &loc)
{
    QString v = (loc.compare("JP", Qt::CaseInsensitive) == 0) ? "JP" : "EN";
    if (v == locale_)
        return;
    locale_ = v;
    Config::instance().setPreferredLocale(v);
    emit localeChanged();
}

QString DatabaseUtil::imageUrlFor(const QString &cardCode) const
{
    bool ok = false;
    const QJsonObject o = fetchCardObject(cardCode, ok);
    return ok ? OfficialFallback::resolveImageUrl(o) : QString();
}

QVariantMap DatabaseUtil::cardDataFor(const QString &cardCode) const
{
    QVariantMap card;
    bool ok = false;
    QJsonObject o = fetchCardObject(cardCode, ok);

    if (!ok)
        return card;

    card["cardId"] = o.value("cardcode").toString();
    card["cardCode"] = o.value("cardcode").toString();
    card["setName"] = o.value("set").toString();
    card["rarity"] = o.value("rarity").toString();
    card["color"] = o.value("colour").toString();
    card["cardKind"] = o.value("cardtype").toString();
    card["cardType"] = o.value("cardtype").toString();

    auto numOrEmpty = [&](const char *k) -> QString {
        QJsonValue v = o.value(k);
        return v.isDouble() ? QString::number(v.toInt()) : v.toString();
    };
    card["power"] = numOrEmpty("power");
    card["soul"] = numOrEmpty("soul");
    card["level"] = numOrEmpty("level");
    card["cost"] = numOrEmpty("cost");

    QStringList trig;
    for (const QJsonValue &t : o.value("trigger").toArray())
        trig << t.toString();
    card["cardTrigger"] = trig.join(", ");

    card["picture"] = OfficialFallback::resolveImageUrl(o);

    auto blockKeyFor = [](const QString &userLoc) -> QString {
        return (userLoc == "JP") ? "NP" : "EN";
    };
    auto localeBlock = [&](const QString &userLoc) -> QJsonObject {
        QJsonObject localeRoot = o.value("locale").toObject();
        return localeRoot.value(blockKeyFor(userLoc)).toObject();
    };
    auto blockHasContent = [](const QJsonObject &b) {
        return !b.value("name").toString().isEmpty() || !b.value("ability").toArray().isEmpty();
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
        card["text"] = "";
        card["flavor"] = "";
        card["feature1"] = "";
        card["feature2"] = "";
        card["features"] = "";
        card["source"] = o.value("_source").toString().isEmpty() ? "encoredecks"
                                                                 : o.value("_source").toString();
        return card;
    }

    QString name = blk.value("name").toString();
    card["cardName"] = name;

    QStringList lines;
    for (const QJsonValue &a : blk.value("ability").toArray())
        lines << a.toString();
    card["text"] = lines.join("\n\n");

    card["flavor"] = blk.value("flavor").toString();

    QStringList attrs;
    for (const QJsonValue &a : blk.value("attributes").toArray())
        attrs << a.toString();
    card["feature1"] = attrs.value(0);
    card["feature2"] = attrs.value(1);
    card["features"] = attrs.join(" / ");

    card["source"] = o.value("_source").toString().isEmpty() ? "encoredecks"
                                                             : o.value("_source").toString();

    return card;
}

bool DatabaseUtil::cardInDb(const QString &cardCode) const
{
    bool ok = false;
    fetchCardObject(cardCode, ok);
    return ok;
}

void DatabaseUtil::ensureCardData(const QString &cardCode)
{
    {
        bool ok = false;
        QJsonObject existing = fetchCardObject(cardCode, ok);
        if (ok) {
            QJsonObject loc = existing.value("locale").toObject();
            const bool hasName = !loc.value("NP").toObject().value("name").toString().isEmpty()
                                 || !loc.value("EN").toObject().value("name").toString().isEmpty();
            if (hasName) {
                QMetaObject::invokeMethod(
                    this, [this, cardCode] { emit cardReady(cardCode); }, Qt::QueuedConnection);
                return;
            }
        }
    }

    if (isKnownMissing(cardCode)) {
        emit cardFetchFailed(cardCode, "No card data available");
        return;
    }

    auto it = fetchState_.find(cardCode);
    if (it != fetchState_.end()) {
        if (it->permanent) {
            emit cardFetchFailed(cardCode, "No card data available");
            return;
        }
        if (QDateTime::currentMSecsSinceEpoch() < it->nextRetryMs) {
            emit cardFetchFailed(cardCode, "Rate limited — try again later");
            return;
        }
    }

    const bool en = isEnMode();
    const QUrl url(en ? OfficialFallback::enPageUrlFromCardcode(cardCode)
                      : OfficialFallback::dataUrlFromCardcode(cardCode));
    qDebug() << "[DatabaseUtil] official fallback (" << (en ? "EN" : "JP") << "):" << url;

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TCGDeckBuilder/1.0");
    QNetworkReply *reply = nam_.get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, cardCode, en] {
        reply->deleteLater();

        if (en != isEnMode()) {
            emit cardFetchFailed(cardCode, "Set language changed — try again");
            return;
        }

        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (http == 404) {
            fetchState_[cardCode].permanent = true;
            emit cardFetchFailed(cardCode, "No card data available");
            markMissing(cardCode, "No card data available");
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            auto &st = fetchState_[cardCode];
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
            qDebug() << "fetch failed" << cardCode << "http" << http << "retry in" << backoff
                     << "ms";

            const QString reason = (http == 429) ? "Rate limited — try again later"
                                                 : "Network error";
            emit cardFetchFailed(cardCode, reason);
            return;
        }

        const QByteArray body = reply->readAll();
        const QJsonObject shaped = en ? OfficialFallback::reshapeEnOfficialHtml(body, cardCode)
                                      : parseJpOfficial(body, cardCode);

        if (shaped.isEmpty()) {
            fetchState_[cardCode].permanent = true;
            qDebug() << "official" << (en ? "EN" : "JP") << ": no card for" << cardCode;
            emit cardFetchFailed(cardCode, "No card data available");
            markMissing(cardCode, "No card data available");
            return;
        }

        storeOfficialCard(cardCode, shaped);
        fetchState_.remove(cardCode);
        emit cardReady(cardCode);

        if (en) {
            QString safe = cardCode;
            safe.replace('/', '_');
            QFile dump(QDir::tempPath() + "/ws_en_" + safe + ".html");
            if (dump.open(QIODevice::WriteOnly))
                dump.write(body);
        }
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

QVariantList DatabaseUtil::searchCards(const QString &query, int limit) const
{
    QVariantList out;
    const QString q = query.trimmed();
    if (q.size() < 2)
        return out;

    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return out;

    QString esc = q;
    esc.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");
    const QString prefix = esc + "%";
    const QString contains = "%" + esc + "%";

    QSqlQuery s(db);
    s.prepare("SELECT cardcode, data FROM cards "
              "WHERE cardcode LIKE ? ESCAPE '\\' "
              "   OR json_extract(data, '$.locale.EN.name') LIKE ? ESCAPE '\\' "
              "   OR json_extract(data, '$.locale.NP.name') LIKE ? ESCAPE '\\' "
              "GROUP BY cardcode "
              "ORDER BY (cardcode LIKE ? ESCAPE '\\') DESC, cardcode "
              "LIMIT ?");
    s.addBindValue(prefix);
    s.addBindValue(contains);
    s.addBindValue(contains);
    s.addBindValue(prefix);
    s.addBindValue(limit);

    if (!s.exec()) {
        qDebug() << "searchCards (json) failed, code-only fallback:" << s.lastError().text();
        s = QSqlQuery(db);
        s.prepare("SELECT cardcode, data FROM cards WHERE cardcode LIKE ? ESCAPE '\\' "
                  "GROUP BY cardcode ORDER BY cardcode LIMIT ?");
        s.addBindValue(prefix);
        s.addBindValue(limit);
        if (!s.exec())
            return out;
    }

    while (s.next()) {
        const QJsonObject o = QJsonDocument::fromJson(s.value(1).toByteArray()).object();
        const QJsonObject loc = o.value("locale").toObject();
        QString name = loc.value(locale_ == "JP" ? "NP" : "EN").toObject().value("name").toString();
        if (name.isEmpty())
            name = loc.value(locale_ == "JP" ? "EN" : "NP").toObject().value("name").toString();

        QVariantMap row;
        row["cardCode"] = s.value(0).toString();
        row["name"] = name;
        row["rarity"] = o.value("rarity").toString();
        row["source"] = o.value("_source").toString();
        out << row;
    }
    return out;
}

void DatabaseUtil::storeOfficialCard(const QString &cardCode, const QJsonObject &shaped)
{
    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return;

    QSqlQuery ins(db);
    ins.prepare("INSERT OR REPLACE INTO cards (series_id, card_id, cardcode, data) "
                "VALUES (?,?,?,?)");
    ins.addBindValue("__official__");
    ins.addBindValue(cardCode);
    ins.addBindValue(cardCode);
    ins.addBindValue(QString::fromUtf8(QJsonDocument(shaped).toJson(QJsonDocument::Compact)));
    if (!ins.exec())
        qDebug() << "storeOfficialCard failed:" << ins.lastError().text();
}

int DatabaseUtil::countAdvanced(const QVariantMap &filters) const
{
    QString where;
    QVariantList binds;
    if (!buildAdvancedWhere(filters, where, binds))
        return 0;
    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return 0;

    QSqlQuery q(db);
    q.prepare("SELECT COUNT(DISTINCT cardcode) FROM cards WHERE " + where);
    for (const QVariant &b : binds)
        q.addBindValue(b);
    if (!q.exec() || !q.next()) {
        qDebug() << "countAdvanced failed:" << q.lastError().text();
        return 0;
    }
    return q.value(0).toInt();
}

QStringList DatabaseUtil::advancedSearchCodes(const QVariantMap &filters) const
{
    QStringList out;
    QString where;
    QVariantList binds;
    if (!buildAdvancedWhere(filters, where, binds))
        return out;
    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return out;

    QSqlQuery q(db);
    q.prepare("SELECT DISTINCT cardcode FROM cards WHERE " + where);
    for (const QVariant &b : binds)
        q.addBindValue(b);
    if (!q.exec()) {
        qDebug() << "advancedSearchCodes failed:" << q.lastError().text();
        return out;
    }
    while (q.next())
        out << q.value(0).toString();
    return out;
}

static QString setCondition(const QStringList &sets, QVariantList &binds)
{
    if (sets.isEmpty())
        return "1";
    QStringList marks;
    for (const QString &s : sets) {
        marks << "?";
        binds << s.toUpper();
    }
    return "UPPER(SUBSTR(cards.cardcode, 1, INSTR(cards.cardcode, '/') - 1)) IN (" + marks.join(',')
           + ")";
}

static QStringList distinctValues(const QString &sql, const QVariantList &binds)
{
    QStringList out;
    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return out;
    QSqlQuery q(db);
    q.prepare(sql);
    for (const QVariant &b : binds)
        q.addBindValue(b);
    if (!q.exec()) {
        qDebug() << "distinctValues failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        const QString v = q.value(0).toString().trimmed();
        if (!v.isEmpty() && v != "-")
            out << v;
    }
    return out;
}

QStringList DatabaseUtil::distinctTraits(const QStringList &sets) const
{
    QVariantList binds;
    const QString condEn = setCondition(sets, binds);
    const QString condNp = setCondition(sets, binds);
    return distinctValues("SELECT j.value FROM cards, "
                          "json_each(json_extract(cards.data,'$.locale.EN.attributes')) AS j "
                          "WHERE "
                              + condEn
                              + " "
                                "UNION "
                                "SELECT j.value FROM cards, "
                                "json_each(json_extract(cards.data,'$.locale.NP.attributes')) AS j "
                                "WHERE "
                              + condNp
                              + " "
                                "ORDER BY 1 COLLATE NOCASE",
                          binds);
}

QStringList DatabaseUtil::distinctTriggers(const QStringList &sets) const
{
    QVariantList binds;
    const QString cond = setCondition(sets, binds);
    return distinctValues("SELECT DISTINCT UPPER(j.value) FROM cards, "
                          "json_each(json_extract(cards.data,'$.trigger')) AS j "
                          "WHERE "
                              + cond + " ORDER BY 1",
                          binds);
}