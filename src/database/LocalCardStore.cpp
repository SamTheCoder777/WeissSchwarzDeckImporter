#include "LocalCardStore.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUrl>
#include <algorithm>

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
    value = value.trimmed().toUpper();
    static const QHash<QString, QString> aliases = {{"SALVAGE", "COMEBACK"},
                                                    {"PANTS", "GATE"},
                                                    {"BAG", "POOL"},
                                                    {"BOUNCE", "RETURN"},
                                                    {"BAR", "TREASURE"}};
    return aliases.value(value, value);
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
            static const QHash<QString, QStringList> aliases = {{"COMEBACK",
                                                                 {"COMEBACK", "SALVAGE"}},
                                                                {"GATE", {"GATE", "PANTS"}},
                                                                {"POOL", {"POOL", "BAG"}},
                                                                {"RETURN", {"RETURN", "BOUNCE"}},
                                                                {"TREASURE", {"TREASURE", "BAR"}}};
            QStringList ors;
            const QString canonical = trigger(tr);
            for (const QString &t : aliases.value(canonical, QStringList{canonical})) {
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
} // namespace LocalCardStore
