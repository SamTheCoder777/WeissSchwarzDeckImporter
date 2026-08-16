#include "OfficialFallback.h"
#include <QUrl>
#include <QJsonArray>
#include <QRegularExpression>

// QString OfficialFallback::imageUrlFromCardcode(const QString& cardcode) {
//     QString code = cardcode.toLower();
//     code.replace('/', '_').replace('-', '_');
//     if (code.isEmpty()) return {};

//     const QString first = code.left(1);
//     const QStringList parts = code.split('_');
//     if (parts.size() < 2) return {};
//     const QString titleSet = parts[0] + "_" + parts[1];

//     return "https://ws-tcg.com/wordpress/wp-content/images/cardlist/"
//            + first + "/" + titleSet + "/" + code + ".png";
// }

QString OfficialFallback::dataUrlFromCardcode(const QString& cardcode) {
    return "https://ws-tcg.com/manage/CardListUser/searchJson?keyword="
           + QString::fromUtf8(QUrl::toPercentEncoding(cardcode));
}

static QString colorFromToken(const QString& s) {
    QRegularExpression re("\\[\\[(\\w+)\\.gif\\]\\]");
    auto m = re.match(s);
    return m.hasMatch() ? m.captured(1).toUpper() : QString();
}
static int soulCount(const QString& s) {
    return s.count("soul.gif");
}
static QString kindToCardtype(const QString& k) {
    if (k == "0") return "CX";
    if (k == "1") return "EV";
    if (k == "2") return "CH";
    return k;
}
static QJsonArray triggerTokens(const QString& s) {
    QJsonArray out;
    if (s.isEmpty() || s == "-") return out;
    QRegularExpression re("\\[\\[(\\w+)\\.gif\\]\\]");
    auto it = re.globalMatch(s);
    while (it.hasNext()) out.append(it.next().captured(1).toUpper());
    return out;
}

QJsonObject OfficialFallback::reshapeOfficialItem(const QJsonObject& o) {
    QJsonArray attrs;
    for (const char* f : {"feature1","feature2","feature3"}) {
        QString v = o.value(f).toString();
        if (!v.isEmpty()) attrs.append(v);
    }
    // ability lines from text split on <br/>
    QJsonArray ability;
    const QString text = o.value("text").toString();
    for (const QString& line : text.split("<br/>")) {
        QString t = line.trimmed();
        if (!t.isEmpty()) ability.append(t);
    }
    const QString flavor = o.value("flavor").toString();

    QJsonObject np;
    np["name"]       = o.value("card_name").toString();
    np["ability"]    = ability;
    np["flavor"]     = (flavor == "-" || flavor.isEmpty()) ? QJsonValue() : flavor;
    np["attributes"] = attrs;

    const QString cardcode = o.value("card_number").toString();
    const QString setCode  = cardcode.section('/', 1).section('-', 0, 0);

    auto toInt = [&](const char* k){ return o.value(k).toString().toInt(); };

    QJsonObject card;
    card["cardcode"]  = cardcode;
    card["set"]       = setCode;
    card["cardtype"]  = kindToCardtype(o.value("card_kind").toString());
    card["colour"]    = colorFromToken(o.value("color").toString());
    card["level"]     = toInt("level");
    card["cost"]      = toInt("cost");
    card["power"]     = toInt("power");
    card["soul"]      = soulCount(o.value("soul").toString());
    card["rarity"]    = o.value("rare").toString();
    card["trigger"]   = triggerTokens(o.value("card_trigger").toString());
    card["imagepath"] = o.value("picture").toString();
    QJsonObject locale;
    locale["EN"] = QJsonObject();
    locale["NP"] = np;
    card["locale"] = locale;
    card["_source"]        = "official";
    card["_localeAvailable"] = false;
    return card;
}
