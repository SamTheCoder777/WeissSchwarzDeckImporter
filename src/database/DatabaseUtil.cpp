#include "DatabaseUtil.h"

#include "../core/Config.h"
#include "../database/CardResolver.h"

#include <QRegularExpression>
#include <algorithm>

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
#include <QUrl>
#include <QUrlQuery>

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
            static const QHash<QString, QStringList> aliases = {
                {"COMEBACK", {"COMEBACK", "SALVAGE"}},
                {"GATE", {"GATE", "PANTS"}},
                {"POOL", {"POOL", "BAG"}},
                {"RETURN", {"RETURN", "BOUNCE"}},
                {"TREASURE", {"TREASURE", "BAR"}},

                {"CHOICE", {"CHOICE"}},
                {"DRAW", {"DRAW"}},
                {"BOOK", {"BOOK"}},
                {"FOCUS", {"FOCUS"}},
                {"SHOT", {"SHOT"}},
                {"SOUL", {"SOUL"}},
                {"STANDBY", {"STANDBY"}},
            };
            const QString key = v.toUpper();
            const QStringList terms = aliases.value(key, QStringList{key});
            QStringList ors;
            for (const QString &t : terms) {
                ors << "UPPER(json_extract(data,'$.trigger')) LIKE ? ESCAPE '\\'";
                binds << likeContains(t);
            }
            conds << "(" + ors.join(" OR ") + ")";
        }
    }
    const QStringList types = f.value("cardType").toStringList();
    if (!types.isEmpty()) {
        QStringList ph;
        for (const QString &t : types) {
            ph << "?";
            binds << t;
        }
        conds << "json_extract(data,'$.cardtype') IN (" + ph.join(",") + ")";
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

static QString baseCardCode(const QString &input)
{
    const QString code = input.trimmed().toUpper();

    static const QRegularExpression re(R"(^([^/\s]+/[^/\s]+-[0-9]++)([A-Za-z]+)?$)");

    const auto match = re.match(code);
    return match.hasMatch() ? match.captured(1) : code;
}

static QList<QJsonObject> encoreCandidates(const QString &input)
{
    const QString requested = input.trimmed().toUpper();
    const QString base = baseCardCode(requested);

    QList<QJsonObject> result;

    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return result;

    QSqlQuery query(db);
    query.prepare("SELECT cardcode, data FROM cards "
                  "WHERE UPPER(cardcode) LIKE ? ESCAPE '\\'");

    QString prefix = base;
    prefix.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");

    query.addBindValue(prefix + "%");

    if (!query.exec()) {
        qWarning() << "encoreCandidates:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        const QString candidateCode = query.value(0).toString().trimmed().toUpper();

        if (baseCardCode(candidateCode) != base)
            continue;

        QJsonParseError error;
        QJsonObject card = QJsonDocument::fromJson(query.value(1).toByteArray(), &error).object();

        if (error.error != QJsonParseError::NoError || card.isEmpty()) {
            continue;
        }

        const QString source = card.value("_source").toString().toLower();

        if (!source.isEmpty() && source != "encoredecks")
            continue;

        card["cardcode"] = query.value(0).toString();
        result.append(card);
    }

    auto priority = [&](const QJsonObject &card) {
        const QString code = card.value("cardcode").toString().toUpper();

        if (code == requested)
            return 0;
        if (code == base)
            return 1;
        return 2;
    };

    std::stable_sort(result.begin(), result.end(), [&](const QJsonObject &a, const QJsonObject &b) {
        const int pa = priority(a);
        const int pb = priority(b);

        if (pa != pb)
            return pa < pb;

        return a.value("cardcode").toString() < b.value("cardcode").toString();
    });

    return result;
}

static bool hasCardData(const QJsonObject &card)
{
    const QJsonObject locales = card.value("locale").toObject();

    for (const QString &key : {QStringLiteral("EN"), QStringLiteral("NP")}) {
        const QJsonObject block = locales.value(key).toObject();

        if (!block.value("name").toString().trimmed().isEmpty()
            || !block.value("ability").toArray().isEmpty()) {
            return true;
        }
    }

    return false;
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
    return imageUrlsFor(cardCode).value(0);
}

QStringList DatabaseUtil::imageUrlsFor(const QString &cardCode) const
{
    QStringList urls;

    for (const QJsonObject &candidate : encoreCandidates(cardCode)) {
        const QString url = CardResolver::imageUrl(candidate);

        if (!url.isEmpty() && !urls.contains(url))
            urls.append(url);
    }

    return urls;
}

QVariantMap DatabaseUtil::cardDataFor(const QString &cardCode) const
{
    QVariantMap card;
    QJsonObject o;

    for (const QJsonObject &candidate : encoreCandidates(cardCode)) {
        if (hasCardData(candidate)) {
            o = candidate;
            break;
        }
    }

    if (o.isEmpty())
        return card;

    card["cardId"] = cardCode;
    card["cardCode"] = cardCode;

    const QString dataCode = o.value("cardcode").toString();

    card["dataCardCode"] = dataCode;
    card["dataFallbackUsed"] = dataCode.compare(cardCode, Qt::CaseInsensitive) != 0;

    card["setName"] = o.value("set").toString();

    bool exactFound = false;
    const QJsonObject exact = fetchCardObject(cardCode, exactFound);

    card["rarity"] = exactFound ? exact.value("rarity").toString() : QString();

    card["dataRarity"] = o.value("rarity").toString();

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

    card["picture"] = imageUrlFor(cardCode);

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
    const QVariantMap card = cardDataFor(cardCode);

    QMetaObject::invokeMethod(
        this,
        [this, cardCode, available = !card.isEmpty()] {
            if (available)
                emit cardReady(cardCode);
            else
                emit cardFetchFailed(cardCode, "No card data available");
        },
        Qt::QueuedConnection);
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

static QString imageMissingKey(const QString &url)
{
    return QStringLiteral("image-url:") + url;
}

bool DatabaseUtil::imageRetryBlocked(const QString &url)
{
    if (url.isEmpty())
        return false;

    ensureMissingTable();

    QSqlDatabase db = getCardDb();
    if (!db.isValid())
        return false;

    QSqlQuery query(db);
    query.prepare("SELECT 1 FROM missing_cards WHERE cardcode = ?");
    query.addBindValue(imageMissingKey(url));

    if (!query.exec()) {
        qWarning() << "imageRetryBlocked:" << query.lastError().text();
        return false;
    }

    return query.next();
}

void DatabaseUtil::recordImageFailure(const QString &url, int httpStatus)
{
    if (url.isEmpty())
        return;

    markMissing(imageMissingKey(url), QStringLiteral("Image load failed: HTTP %1").arg(httpStatus));
}

void DatabaseUtil::purgeMissingCards()
{
    QSqlDatabase db = getCardDb();
    if (!db.isValid()) {
        qWarning() << "purgeMissingCards: invalid database";
        return;
    }

    ensureMissingTable();

    QSqlQuery q(db);
    int count = 0;

    if (q.exec("SELECT COUNT(*) FROM missing_cards") && q.next())
        count = q.value(0).toInt();

    q.finish();

    if (!q.exec("DELETE FROM missing_cards")) {
        qWarning() << "purgeMissingCards failed:" << q.lastError().text();
        return;
    }

    const bool diskCleared = CardImageProvider::clearImageCaches();

    const QStringList changedCodes = imageFallback_.keys();
    imageFallback_.clear();

    for (const QString &code : changedCodes)
        emit imageFallbackChanged(code);

    qDebug() << "Purged" << count << "missing records;"
             << "image caches cleared; disk success:" << diskCleared;

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

bool DatabaseUtil::imageUsesOtherRarity(const QString &cardCode) const
{
    return imageFallback_.value(cardCode.trimmed().toUpper(), false);
}

void DatabaseUtil::reportLoadedImage(const QString &cardCode, const QString &url)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this, [this, cardCode, url] { reportLoadedImage(cardCode, url); }, Qt::QueuedConnection);
        return;
    }

    bool found = false;
    const QJsonObject exact = fetchCardObject(cardCode, found);

    const QString exactUrl = found ? CardResolver::imageUrl(exact) : QString();

    const bool fallback = !url.isEmpty() && (exactUrl.isEmpty() || QUrl(url) != QUrl(exactUrl));

    const QString key = cardCode.trimmed().toUpper();

    if (imageFallback_.contains(key) && imageFallback_.value(key) == fallback) {
        return;
    }

    imageFallback_.insert(key, fallback);
    emit imageFallbackChanged(cardCode);
}

// Give official card website url
QString DatabaseUtil::officialCardPageUrl(const QString &cardCode) const
{
    QUrl url(isEnMode() ? "https://en.ws-tcg.com/cardlist/" : "https://ws-tcg.com/cardlist/");

    QUrlQuery query;
    query.addQueryItem("cardno", cardCode.trimmed());
    url.setQuery(query);

    return url.toString(QUrl::FullyEncoded);
}

bool DatabaseUtil::hasExactImageUrl(const QString &cardCode, const QString &url) const
{
    bool found = false;
    const QJsonObject card = fetchCardObject(cardCode, found);

    if (!found)
        return false;

    const QString exact = CardResolver::imageUrl(card);

    return !exact.isEmpty() && QUrl(exact) == QUrl(url);
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
    QStringList raw = distinctValues("SELECT DISTINCT UPPER(j.value) FROM cards, "
                                     "json_each(json_extract(cards.data,'$.trigger')) AS j "
                                     "WHERE "
                                         + cond + " ORDER BY 1",
                                     binds);

    static const QHash<QString, QString> canon = {
        {"CHOICE", "CHOICE"},
        {"COMEBACK", "COMEBACK"},
        {"SALVAGE", "COMEBACK"},
        {"DRAW", "DRAW"},
        {"BOOK", "BOOK"},
        {"FOCUS", "FOCUS"},
        {"GATE", "GATE"},
        {"PANTS", "GATE"},
        {"POOL", "POOL"},
        {"BAG", "POOL"},
        {"RETURN", "RETURN"},
        {"BOUNCE", "RETURN"},
        {"SHOT", "SHOT"},
        {"SOUL", "SOUL"},
        {"STANDBY", "STANDBY"},
        {"TREASURE", "TREASURE"},
        {"BAR", "TREASURE"},
    };

    QStringList out;
    for (const QString &t : raw) {
        const QString c = canon.value(t, t);
        if (!out.contains(c))
            out.append(c);
    }
    out.sort();
    return out;
}