#include "LocalCardStore.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUrl>
#include <algorithm>

#include "ReferenceSearch.h"
#include "TriggerAliases.h"

namespace LocalCardStore {
static const char *projection = R"SQL(
    WITH c AS (
     SELECT *,
        CASE UPPER(TRIM(COALESCE(CAST(card_kind AS TEXT), '')))
        WHEN '1' THEN 'CH'
        WHEN '2' THEN 'CH'
        WHEN '3' THEN 'EV'
        WHEN '4' THEN 'CX'
    
        WHEN 'CHARACTER' THEN 'CH'
        WHEN 'CH' THEN 'CH'
        WHEN 'キャラ' THEN 'CH'
    
        WHEN 'EVENT' THEN 'EV'
        WHEN 'EV' THEN 'EV'
        WHEN 'イベント' THEN 'EV'
    
        WHEN 'CLIMAX' THEN 'CX'
        WHEN 'CX' THEN 'CX'
        WHEN 'クライマックス' THEN 'CX'
    
        ELSE UPPER(TRIM(COALESCE(CAST(card_kind AS TEXT), '')))
        END AS kind_code,
      UPPER(REPLACE(COALESCE(color,''),CHAR(92),'')) AS color_icons,
      UPPER(REPLACE(COALESCE(card_trigger,''),CHAR(92),'')) AS trigger_icons
     FROM cards WHERE set_edition = ?
    )
    )SQL";
static QString cte()
{
    return QString::fromUtf8(projection);
}
static QString contains(QString text)
{
    text.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");
    return "%" + text + "%";
}
static QString deckSetCodeSql()
{
    return QStringLiteral(
        "UPPER(TRIM(CASE "
        "WHEN INSTR(card_number,'/')>0 THEN SUBSTR(card_number,1,INSTR(card_number,'/')-1) "
        "WHEN INSTR(card_number,'-')>0 THEN SUBSTR(card_number,1,INSTR(card_number,'-')-1) "
        "ELSE CAST(title_number AS TEXT) END))");
}

void close(const QString &path)
{
    const QString name = "local_cards_"
                         + QString::fromLatin1(
                             QCryptographicHash::hash(QFileInfo(path).absoluteFilePath().toUtf8(),
                                                      QCryptographicHash::Sha256)
                                 .toHex())
                         + "_" + QString::number(quintptr(QThread::currentThreadId()));
    if (QSqlDatabase::contains(name))
        QSqlDatabase::database(name, false).close();
}
QSqlDatabase open(const QString &path, QString *error)
{
    auto fail = [&](const QString &message) -> QSqlDatabase {
        if (error)
            *error = message;
        qWarning() << "[LocalDB]" << message;
        return {};
    };
    const QFileInfo file(path);
    if (!file.isFile())
        return fail("Database file not found: " + path);
    const QString name = "local_cards_"
                         + QString::fromLatin1(
                             QCryptographicHash::hash(file.absoluteFilePath().toUtf8(),
                                                      QCryptographicHash::Sha256)
                                 .toHex())
                         + "_" + QString::number(quintptr(QThread::currentThreadId()));
    QSqlDatabase db = QSqlDatabase::contains(name) ? QSqlDatabase::database(name, false)
                                                   : QSqlDatabase::addDatabase("QSQLITE", name);
    if (!db.isOpen()) {
        db.setDatabaseName(file.absoluteFilePath());
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (!db.open())
            return fail(db.lastError().text());
        QSqlQuery schema(db);
        if (!schema.exec(
                "SELECT card_number,set_edition,set_name,card_name,card_name_kana,"
                "card_kind,color,level,cost,power,soul,card_trigger,rare,feature1,"
                "feature2,feature3,text,translated_text,flavor,picture FROM cards LIMIT 0")) {
            const QString reason = schema.lastError().text();
            schema.finish();
            db.close();
            return fail("Incompatible customer schema: " + reason);
        }
    }
    if (error)
        error->clear();
    return db;
}
QString kind(QString value)
{
    const QString normalized = value.trimmed().toUpper();

    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("2")
        || normalized == QStringLiteral("CHARACTER") || normalized == QStringLiteral("CH")
        || normalized == QStringLiteral("キャラ")) {
        return QStringLiteral("CH");
    }

    if (normalized == QStringLiteral("3") || normalized == QStringLiteral("EVENT")
        || normalized == QStringLiteral("EV") || normalized == QStringLiteral("イベント")) {
        return QStringLiteral("EV");
    }

    if (normalized == QStringLiteral("4") || normalized == QStringLiteral("CLIMAX")
        || normalized == QStringLiteral("CX") || normalized == QStringLiteral("クライマックス")) {
        return QStringLiteral("CX");
    }

    return value.trimmed();
}
QStringList icons(QString value)
{
    value.replace("\\.", ".");
    static const QRegularExpression rx(R"(\[\[([A-Za-z0-9_-]+)\.gif\]\])",
                                       QRegularExpression::CaseInsensitiveOption);
    QStringList out;
    auto it = rx.globalMatch(value);
    while (it.hasNext())
        out << it.next().captured(1).toUpper();
    return out;
}
QString trigger(QString value)
{
    return TriggerAliases::canonical(value);
}
QString plainText(QString value)
{
    static const QRegularExpression br(R"(<br\s*/?>)", QRegularExpression::CaseInsensitiveOption);
    value.replace(br, "\n\n").replace("\\.", ".");
    static const QRegularExpression icon(R"(\[\[([A-Za-z0-9_-]+)\.gif\]\])",
                                         QRegularExpression::CaseInsensitiveOption);
    return value.replace(icon, QStringLiteral("[\\1]"));
}
QString officialImage(const QString &picture, const QString &edition)
{
    const QString p = picture.trimmed();
    if (p.isEmpty())
        return {};
    QUrl url(p);
    if (url.isRelative()) {
        if (edition != "jp" || p.startsWith('/'))
            return {};
        url = QUrl("https://ws-tcg.com/wordpress/wp-content/images/cardlist/").resolved(url);
        if (!url.path().startsWith("/wordpress/wp-content/images/cardlist/"))
            return {};
    }
    const QString host = url.host().toLower();
    if (!url.isValid() || url.scheme() != "https" || !url.userInfo().isEmpty()
        || url.port(443) != 443 || !url.fragment().isEmpty()
        || (host != "ws-tcg.com" && host != "en.ws-tcg.com")
        || !url.path().startsWith("/wordpress/wp-content/images/"))
        return {};
    return url.toString(QUrl::FullyEncoded);
}
QVariantMap card(const QString &path,
                 const QString &edition,
                 const QString &code,
                 const QString &language)
{
    QSqlDatabase db = open(path);
    if (!db.isOpen())
        return {};
    QSqlQuery q(db);
    q.prepare("SELECT card_number,set_edition,set_name,card_name,card_name_kana,card_kind,color,"
              "level,cost,power,soul,card_trigger,rare,feature1,feature2,feature3,text,"
              "translated_text,flavor,picture FROM cards WHERE set_edition=? "
              "AND card_number=? COLLATE NOCASE LIMIT 1");
    q.addBindValue(edition);
    q.addBindValue(code.trimmed());
    if (!q.exec()) {
        qWarning() << "[CustomerDB] lookup:" << q.lastError();
        return {};
    }
    if (!q.next())
        return {};
    auto f = [&](const char *key) { return q.value(QString::fromLatin1(key)).toString(); };
    QVariantMap m;
    m["cardId"] = code;
    m["cardCode"] = code;
    m["dataCardCode"] = f("card_number");
    m["dataFallbackUsed"] = false;
    m["setEdition"] = edition;
    m["source"] = "localdb";
    m["setName"] = f("set_name");
    m["cardName"] = f("card_name");
    m["cardNameKana"] = f("card_name_kana");
    m["rarity"] = f("rare");
    m["dataRarity"] = f("rare");
    m["cardKind"] = kind(f("card_kind"));
    m["cardType"] = m["cardKind"];
    QStringList colors = icons(f("color"));
    m["color"] = colors.isEmpty() ? f("color").trimmed().toUpper() : colors.join(", ");
    for (const char *key : {"level", "cost", "power", "soul"})
        m[key] = f(key);
    QStringList tr = icons(f("card_trigger"));
    for (QString &t : tr)
        t = trigger(t);
    const QString raw = f("card_trigger").trimmed();
    m["cardTrigger"] = tr.isEmpty() ? (raw == "-" ? QString() : raw) : tr.join(", ");
    QStringList traits;
    for (const char *key : {"feature1", "feature2", "feature3"}) {
        const QString value = f(key).trimmed();
        m[key] = value == "-" ? QString() : value;
        if (!value.isEmpty() && value != "-")
            traits << value;
    }
    m["features"] = traits.join(" / ");
    const QString original = f("text"), translated = f("translated_text");
    const bool useTranslation = edition == "jp" && language == "EN"
                                && !translated.trimmed().isEmpty();
    m["text"] = plainText(useTranslation ? translated : original);
    m["originalText"] = original;
    m["translatedText"] = translated;
    m["translationUsed"] = useTranslation;
    m["abilityLanguage"] = (edition == "en" || useTranslation) ? "EN" : "JP";
    m["locale"] = m["abilityLanguage"];
    m["localeAvailable"] = true;
    m["flavor"] = plainText(f("flavor"));
    m["localPictureReference"] = f("picture");
    m["picture"] = officialImage(f("picture"), edition);
    return m;
}
QVariantList search(const QString &path, const QString &edition, const QString &input, int limit)
{
    const QString text = input.trimmed();
    if (text.size() < 2)
        return {};
    QSqlDatabase db = open(path);
    if (!db.isOpen())
        return {};
    QString prefix = contains(text);
    prefix.remove(0, 1);
    QSqlQuery q(db);
    q.prepare("SELECT card_number,card_name,rare FROM cards WHERE set_edition=? AND "
              "(card_number LIKE ? ESCAPE '\\' OR card_name LIKE ? ESCAPE '\\' "
              "OR card_name_kana LIKE ? ESCAPE '\\') "
              "ORDER BY (card_number=? COLLATE NOCASE) DESC,"
              "(card_number LIKE ? ESCAPE '\\') DESC,card_number LIMIT ?");
    q.addBindValue(edition);
    q.addBindValue(contains(text));
    q.addBindValue(contains(text));
    q.addBindValue(contains(text));
    q.addBindValue(text);
    q.addBindValue(prefix);
    q.addBindValue(qBound(1, limit, 200));
    if (!q.exec()) {
        qWarning() << "[CustomerDB] search:" << q.lastError();
        return {};
    }
    QVariantList out;
    while (q.next())
        out << QVariantMap{{"cardCode", q.value(0)},
                           {"name", q.value(1)},
                           {"rarity", q.value(2)},
                           {"source", "localdb"}};
    return out;
}

static QString where(const QVariantMap &f, QVariantList &binds, bool &filtered)
{
    QStringList terms;
    auto value = [&](const char *key) { return f.value(key).toString().trimmed(); };
    auto like = [&](const QString &column, const QString &v) {
        terms << column + " LIKE ? ESCAPE '\\'";
        binds << contains(v);
    };
    if (!value("name").isEmpty()) {
        terms << "(card_name LIKE ? ESCAPE '\\' OR card_name_kana LIKE ? ESCAPE '\\')";
        binds << contains(value("name")) << contains(value("name"));
    }
    if (!value("code").isEmpty())
        like("card_number", value("code"));
    if (!value("text").isEmpty()) {
        terms << "(text LIKE ? ESCAPE '\\' OR translated_text LIKE ? ESCAPE '\\')";
        binds << contains(value("text")) << contains(value("text"));
    }
    if (!value("trait").isEmpty()) {
        terms << "(feature1=? COLLATE NOCASE OR feature2=? COLLATE NOCASE OR feature3=? COLLATE "
                 "NOCASE)";
        binds << value("trait") << value("trait") << value("trait");
    }
    if (!value("rarity").isEmpty()) {
        terms << "UPPER(TRIM(rare))=?";
        binds << value("rarity").toUpper();
    }
    const QString tr = value("trigger");
    if (!tr.isEmpty()) {
        if (tr.compare("none", Qt::CaseInsensitive) == 0)
            terms << "TRIM(COALESCE(card_trigger,'')) IN ('','-')";
        else {
            QStringList ors;
            const QString canonical = trigger(tr);
            for (const QString &t : TriggerAliases::terms(canonical)) {
                ors << "INSTR(trigger_icons,?)>0";
                binds << "[[" + t + ".GIF]]";
            }
            terms << "(" + ors.join(" OR ") + ")";
        }
    }
    QStringList kinds;
    for (const QString &t : f.value("cardType").toStringList())
        if (!t.trimmed().isEmpty())
            kinds << kind(t).toUpper();
    if (!kinds.isEmpty()) {
        QStringList marks;
        for (const QString &t : kinds) {
            marks << "?";
            binds << t;
        }
        terms << "kind_code IN (" + marks.join(',') + ")";
    }
    const QStringList colors = f.value("colors").toStringList();
    if (!colors.isEmpty()) {
        QStringList ors;
        for (const QString &color : colors) {
            ors << "(INSTR(color_icons,?)>0 OR TRIM(color_icons)=?)";
            binds << "[[" + color.toUpper() + ".GIF]]" << color.toUpper();
        }
        terms << "(" + ors.join(" OR ") + ")";
    }
    for (const QString &column :
         {QString("level"), QString("cost"), QString("power"), QString("soul")}) {
        for (const QString &suffix : {QString("Min"), QString("Max")}) {
            const QString raw = f.value(column + suffix).toString().trimmed();
            if (raw.isEmpty())
                continue;
            bool ok = false;
            const qlonglong n = raw.toLongLong(&ok);
            if (!ok) {
                terms << "0";
                continue;
            }
            const QString v = "TRIM(COALESCE(CAST(" + column + " AS TEXT),''))";
            terms << "(" + v + "<>'' AND " + v + " NOT GLOB '*[^0-9]*' AND CAST(" + column
                         + " AS INTEGER) " + (suffix == "Min" ? ">=" : "<=") + " ?)";
            binds << n;
        }
    }
    filtered = !terms.isEmpty();
    return filtered ? terms.join(" AND ") : QStringLiteral("1");
}
int count(const QString &path, const QString &edition, const QVariantMap &filters)
{
    QVariantList values{edition};
    bool filtered;
    const QString conditions = where(filters, values, filtered);
    if (!filtered)
        return 0;
    QSqlDatabase db = open(path);
    if (!db.isOpen())
        return 0;
    QSqlQuery q(db);
    q.prepare(cte() + "SELECT COUNT(*) FROM c WHERE " + conditions);
    for (const QVariant &v : values)
        q.addBindValue(v);
    if (!q.exec() || !q.next()) {
        qWarning() << "[CustomerDB] count:" << q.lastError();
        return 0;
    }
    return q.value(0).toInt();
}
QStringList codes(const QString &path, const QString &edition, const QVariantMap &filters)
{
    QVariantList values{edition};
    bool filtered;
    const QString conditions = where(filters, values, filtered);
    if (!filtered)
        return {};
    QSqlDatabase db = open(path);
    if (!db.isOpen())
        return {};
    QSqlQuery q(db);
    q.prepare(cte() + "SELECT card_number FROM c WHERE " + conditions + " ORDER BY card_number");
    for (const QVariant &v : values)
        q.addBindValue(v);
    if (!q.exec()) {
        qWarning() << "[CustomerDB] codes:" << q.lastError();
        return {};
    }
    QStringList out;
    while (q.next())
        out << q.value(0).toString();
    return out;
}
static QString scope(const QStringList &sets, QVariantList &values)
{
    if (sets.isEmpty())
        return "1";
    QStringList marks;
    for (const QString &set : sets) {
        marks << "?";
        values << set.toUpper();
    }
    return "UPPER(SUBSTR(card_number,1,INSTR(card_number,'/')-1)) IN (" + marks.join(',') + ")";
}
QStringList traits(const QString &path, const QString &edition, const QStringList &sets)
{
    QSqlDatabase db = open(path);
    if (!db.isOpen())
        return {};
    QVariantList values{edition};
    const QString conditions = scope(sets, values);
    QSqlQuery q(db);
    q.prepare(cte() + ", scoped AS (SELECT * FROM c WHERE " + conditions
              + ") "
                "SELECT feature1 FROM scoped UNION SELECT feature2 FROM scoped UNION SELECT "
                "feature3 FROM scoped");
    for (const QVariant &v : values)
        q.addBindValue(v);
    if (!q.exec()) {
        qWarning() << "[CustomerDB] traits:" << q.lastError();
        return {};
    }
    QStringList out;
    while (q.next()) {
        const QString s = q.value(0).toString().trimmed();
        if (!s.isEmpty() && s != "-" && !out.contains(s))
            out << s;
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}
QStringList triggers(const QString &path, const QString &edition, const QStringList &sets)
{
    QSqlDatabase db = open(path);
    if (!db.isOpen())
        return {};
    QVariantList values{edition};
    const QString conditions = scope(sets, values);
    QSqlQuery q(db);
    q.prepare(cte() + "SELECT DISTINCT card_trigger FROM c WHERE " + conditions);
    for (const QVariant &v : values)
        q.addBindValue(v);
    if (!q.exec()) {
        qWarning() << "[CustomerDB] triggers:" << q.lastError();
        return {};
    }
    QStringList out;
    while (q.next())
        for (const QString &i : icons(q.value(0).toString())) {
            const QString t = trigger(i);
            if (!out.contains(t))
                out << t;
        }
    out.sort();
    return out;
}

// deck specific
QVariantMap deckTitles(const QString &path, const QString &edition)
{
    QVariantMap result{{"titles", QVariantList{}}, {"error", QString()}};
    if (edition != "jp" && edition != "en") {
        result["error"] = "Invalid edition.";
        return result;
    }
    QString error;
    QSqlDatabase db = open(path, &error);
    if (!db.isOpen()) {
        result["error"] = error;
        return result;
    }
    QSqlQuery query(db);
    query.prepare("SELECT TRIM(CAST(title_number AS TEXT)) AS title_id, "
                  "GROUP_CONCAT(DISTINCT NULLIF(TRIM(set_name),'')), COUNT(*) "
                  "FROM cards WHERE set_edition=? AND title_number IS NOT NULL "
                  "AND TRIM(CAST(title_number AS TEXT))<>'' "
                  "GROUP BY TRIM(CAST(title_number AS TEXT)) "
                  "ORDER BY title_id COLLATE NOCASE");
    query.addBindValue(edition);
    if (!query.exec()) {
        result["error"] = query.lastError().text();
        return result;
    }
    QVariantList titles;
    while (query.next()) {
        const QString id = query.value(0).toString();
        const QString names = query.value(1).toString();
        titles.append(QVariantMap{{"titleNumber", id},
                                  {"setNames", names},
                                  {"cardCount", query.value(2).toInt()},
                                  {"label", id + " · " + names}});
    }
    result["titles"] = titles;
    return result;
}

static QString deckNumberValid(const QString &column)
{
    const QString text = "TRIM(COALESCE(CAST(" + column + " AS TEXT),''))";
    return "(" + text + "<>'' AND " + text + " NOT GLOB '*[^0-9]*')";
}

static QString deckColorRank()
{
    QStringList present;
    const QString tokens = "REPLACE(REPLACE(REPLACE(REPLACE(REPLACE(color_icons,CHAR(9),','),"
                           "CHAR(10),','),CHAR(13),','),' ',','),'/',',')";
    for (const QString &color :
         {QString("RED"), QString("YELLOW"), QString("GREEN"), QString("BLUE")})
        present << "(INSTR(color_icons,'[[" + color
                       + ".GIF]]')>0 OR "
                         "INSTR(','||"
                       + tokens + "||',','," + color + ",')>0)";
    return "CASE WHEN (" + present.join("+")
           + ")>1 THEN 4 "
             "WHEN "
           + present[0] + " THEN 0 WHEN " + present[1]
           + " THEN 1 "
             "WHEN "
           + present[2] + " THEN 2 WHEN " + present[3] + " THEN 3 ELSE 5 END";
}

static QString deckOrder(const QVariantMap &filters, QString &groupKey, QString &groupLabel)
{
    const QString sort = filters.value("_deckSort", "code").toString();
    const bool composite = sort == "colorLevelCostPower" || sort == "levelPowerColor";
    QString group = filters.value("_deckGroup", "none").toString();
    if (composite)
        group = "color";
    const QString direction = filters.value("_deckDescending", false).toBool() ? " DESC" : " ASC";
    QStringList order;
    groupKey = "''";
    groupLabel = "''";
    if (group == "color") {
        const QString rank = "(" + deckColorRank() + ")";
        order << rank + " ASC";
        groupKey = "CAST(" + rank + " AS TEXT)";
        groupLabel = "CASE " + rank
                     + " WHEN 0 THEN 'Red' WHEN 1 THEN 'Yellow' "
                       "WHEN 2 THEN 'Green' WHEN 3 THEN 'Blue' WHEN 4 THEN 'Multicolor' ELSE "
                       "'Other colors' END";
    } else if (group == "level") {
        const QString valid = deckNumberValid("level");
        order << "CASE WHEN kind_code='CX' THEN 2 WHEN " + valid + " THEN 0 ELSE 1 END ASC";
        order << "CASE WHEN kind_code<>'CX' AND " + valid + " THEN CAST(level AS INTEGER) END"
                     + direction;
        groupKey = "CASE WHEN kind_code='CX' THEN 'climax' WHEN " + valid
                   + " THEN 'level:'||CAST(CAST(level AS INTEGER) AS TEXT) ELSE 'other' END";
        groupLabel
            = "CASE WHEN kind_code='CX' THEN 'Climax' WHEN " + valid
              + " THEN 'Level '||CAST(CAST(level AS INTEGER) AS TEXT) ELSE 'Other levels' END";
    }
    auto number = [&](const QString &column) {
        order << "CASE WHEN " + deckNumberValid(column) + " THEN 0 ELSE 1 END ASC";
        order << "CAST(" + column + " AS INTEGER)" + direction;
    };
    if (sort == "level" || composite)
        number("level");
    if (composite)
        number("cost");
    if (sort == "power" || composite)
        number("power");
    if (sort == "name")
        order << "card_name COLLATE NOCASE" + direction;
    if (sort == "code")
        order << "card_number COLLATE NOCASE" + direction;
    order << "card_number COLLATE NOCASE ASC"; // Stable tie-breaker across pages.
    return order.join(", ");
}

QVariantMap deckPage(const QString &path,
                     const QString &edition,
                     const QString &titleNumber,
                     const QVariantMap &filters,
                     const QString &language,
                     int offset,
                     int limit)
{
    QVariantMap result{{"cards", QVariantList{}}, {"total", 0}, {"error", QString()}};
    if ((edition != "jp" && edition != "en")
        || (!filters.value("_deckReferenceDeckOnly").toBool() && titleNumber.trimmed().isEmpty()))
        return result;
    offset = qMax(0, offset);
    limit = qBound(1, limit, 100);
    QString error;
    QSqlDatabase db = open(path, &error);
    if (!db.isOpen()) {
        result["error"] = error;
        return result;
    }
    QVariantList values{edition};
    bool filtered = false;
    QString conditions = where(filters, values, filtered);
    const QString quickSearch = filters.value("_deckQuickSearch").toString().trimmed();
    if (!quickSearch.isEmpty()) {
        conditions += " AND (card_name LIKE ? ESCAPE '\\' OR card_number LIKE ? ESCAPE '\\')";
        values << contains(quickSearch) << contains(quickSearch);
    }
    QRegularExpression referenceAbilityPattern;
    bool abilityReference = false;
    bool nameReference = false;
    QString referenceName;
    const QString referenceKind = filters.value("_deckReferenceKind").toString();
    QString referenceTerm = filters.value("_deckReferenceTerm").toString().trimmed();
    if (!referenceKind.isEmpty()) {
        if (referenceTerm.isEmpty()) {
            conditions += " AND 0";
        } else if (referenceKind == "trait") {
            conditions += " AND (feature1=? COLLATE NOCASE OR feature2=? COLLATE NOCASE "
                          "OR feature3=? COLLATE NOCASE)";
            values << referenceTerm << referenceTerm << referenceTerm;
        } else if (referenceKind == "name") {
            referenceName = ReferenceSearch::compactName(referenceTerm);
            nameReference = true;
        } else if (referenceKind == "ability") {
            referenceAbilityPattern = ReferenceSearch::abilityPattern(referenceTerm);
            abilityReference = true;
        } else {
            conditions += " AND 0";
        }
    }
    const QStringList setCodes = filters.value("_deckSetCodes").toStringList();
    if (filters.value("_deckReferenceDeckOnly").toBool()) {
        const QStringList codes = filters.value("_deckReferenceCodes").toStringList();
        if (codes.isEmpty()) {
            conditions += " AND 0"; // Empty deck must never fall back to a full-set query.
        } else {
            QStringList marks;
            for (const QString &code : codes) {
                marks << "?";
                values << code;
            }
            conditions += " AND card_number IN (" + marks.join(',') + ")";
        }
    } else if (!setCodes.isEmpty()) {
        QStringList marks;
        for (const QString &code : setCodes) {
            marks << "?";
            values << code.trimmed().toUpper();
        }
        conditions += " AND " + deckSetCodeSql() + " IN (" + marks.join(',') + ")";
    } else {
        // Compatibility for older callers that still pass one DB title_number.
        conditions += " AND TRIM(CAST(title_number AS TEXT))=?";
        values << titleNumber.trimmed();
    }

    const bool textReference = abilityReference || nameReference;
    if (!textReference) {
        QSqlQuery countQuery(db);
        countQuery.prepare(cte() + "SELECT COUNT(*) FROM c WHERE " + conditions);
        for (const QVariant &value : values)
            countQuery.addBindValue(value);
        if (!countQuery.exec() || !countQuery.next()) {
            result["error"] = countQuery.lastError().text();
            return result;
        }
        result["total"] = countQuery.value(0).toInt();
    }
    QString groupKey, groupLabel;
    const QString ordering = deckOrder(filters, groupKey, groupLabel);
    QSqlQuery query(db);
    QString sql = cte() + "SELECT card_number," + groupKey + "," + groupLabel;
    if (abilityReference)
        sql += ",text,translated_text";
    else if (nameReference)
        sql += ",card_name,NULL";
    sql += " FROM c WHERE " + conditions + " ORDER BY " + ordering;
    if (!textReference)
        sql += " LIMIT ? OFFSET ?";
    query.prepare(sql);
    for (const QVariant &value : values)
        query.addBindValue(value);
    if (!textReference) {
        query.addBindValue(limit);
        query.addBindValue(offset);
    }
    if (!query.exec()) {
        result["error"] = query.lastError().text();
        return result;
    }
    struct Hit
    {
        QString code, group, label;
    };
    QVector<Hit> hits;
    int exactTotal = 0;
    while (query.next()) {
        if (textReference) {
            if (nameReference
                && !ReferenceSearch::matchesName(referenceName, query.value(3).toString()))
                continue;
            if (abilityReference
                && !ReferenceSearch::matchesAbility(referenceAbilityPattern,
                                                    query.value(3).toString())
                && !ReferenceSearch::matchesAbility(referenceAbilityPattern,
                                                    query.value(4).toString()))
                continue;
            const int matchIndex = exactTotal++;
            if (matchIndex < offset || hits.size() >= limit)
                continue;
        }
        hits.append(
            Hit{query.value(0).toString(), query.value(1).toString(), query.value(2).toString()});
    }
    if (query.lastError().isValid()) {
        result["error"] = query.lastError().text();
        return result;
    }
    query.finish();
    if (textReference)
        result["total"] = exactTotal;
    QVariantList cards;
    for (const Hit &hit : hits) {
        QVariantMap data = card(path, edition, hit.code, language);
        if (data.isEmpty()) {
            result["error"] = "A card changed or became unavailable during the search. Try again.";
            return result;
        }
        data["deckGroup"] = hit.group;
        data["deckGroupLabel"] = hit.label;
        cards.append(data);
    }
    result["cards"] = cards;
    return result;
}

static QStringList deckDescriptionCodes(const QString &description)
{
    static const QRegularExpression pattern(QStringLiteral(R"(\bFor\s+sets\s*:\s*\[([^\[\]]*)\])"),
                                            QRegularExpression::CaseInsensitiveOption);

    const auto match = pattern.match(description);
    if (!match.hasMatch())
        return {};

    QStringList codes;
    QSet<QString> seen;

    for (const QString &part : match.captured(1).split(',', Qt::SkipEmptyParts)) {
        const QString code = part.trimmed().toUpper();

        if (!code.isEmpty() && !seen.contains(code)) {
            seen.insert(code);
            codes.append(code);
        }
    }

    return codes;
}

QVariantMap deckGroups(const QString &path, const QString &edition, const QJsonObject &manifest)
{
    QVariantMap result{{"titles", QVariantList{}}, {"error", QString()}, {"notice", QString()}};
    if (edition != "jp" && edition != "en") {
        result["error"] = "Invalid edition.";
        return result;
    }
    QString error;
    QSqlDatabase db = open(path, &error);
    if (!db.isOpen()) {
        result["error"] = error;
        return result;
    }
    QSqlQuery query(db);
    query.prepare("SELECT " + deckSetCodeSql()
                  + " AS set_code, COUNT(*) "
                    "FROM cards WHERE set_edition=? GROUP BY set_code ORDER BY set_code");
    query.addBindValue(edition);
    if (!query.exec()) {
        result["error"] = query.lastError().text();
        return result;
    }
    QMap<QString, int> counts;
    while (query.next()) {
        const QString code = query.value(0).toString();
        if (!code.isEmpty())
            counts.insert(code, query.value(1).toInt());
    }
    query.finish();
    QVariantList groups;
    QSet<QString> mapped, ids;
    for (const QJsonValue &value : manifest.value("indexes").toArray()) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString entryEdition = object.value("edition").toString().trimmed().toLower();
        if (!entryEdition.isEmpty() && entryEdition != edition)
            continue;
        const QString id = object.value("id").toString().trimmed();
        if (id.isEmpty() || ids.contains(id))
            continue;
        const QStringList codes = deckDescriptionCodes(object.value("description").toString());
        if (codes.isEmpty())
            continue;
        int count = 0;
        for (const QString &code : codes)
            count += counts.value(code);
        if (!count)
            continue;
        ids.insert(id);
        for (const QString &code : codes)
            mapped.insert(code);
        QString name = object.value("name").toString(id).trimmed();
        if (name.isEmpty())
            name = id;
        groups.append(QVariantMap{{"titleNumber", "manifest:" + id},
                                  {"setNames", name},
                                  {"setCodes", codes},
                                  {"cardCount", count},
                                  {"label", name + " · " + codes.join(" / ")},
                                  {"mapped", true}});
    }
    std::sort(groups.begin(), groups.end(), [](const QVariant &left, const QVariant &right) {
        return QString::localeAwareCompare(left.toMap().value("setNames").toString(),
                                           right.toMap().value("setNames").toString())
               < 0;
    });
    int unmapped = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (mapped.contains(it.key()))
            continue;
        ++unmapped;
        const QString name = "Unmapped · " + it.key();
        groups.append(QVariantMap{{"titleNumber", "code:" + it.key()},
                                  {"setNames", name},
                                  {"setCodes", QStringList{it.key()}},
                                  {"cardCount", it.value()},
                                  {"label", name},
                                  {"mapped", false}});
    }
    result["titles"] = groups;
    if (!manifest.value("indexes").isArray())
        result["notice"]
            = "No saved FAISS manifest for this edition. Check FAISS updates, then reload sets.";
    else if (unmapped)
        result["notice"] = QString("%1 local codes have no named manifest group.").arg(unmapped);
    return result;
}

void releaseThreadConnection(const QString &path)
{
    const QString name = "local_cards_"
                         + QString::fromLatin1(
                             QCryptographicHash::hash(QFileInfo(path).absoluteFilePath().toUtf8(),
                                                      QCryptographicHash::Sha256)
                                 .toHex())
                         + "_" + QString::number(quintptr(QThread::currentThreadId()));
    if (QSqlDatabase::contains(name))
        QSqlDatabase::removeDatabase(name);
}

} // namespace LocalCardStore
