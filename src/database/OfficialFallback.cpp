#include "OfficialFallback.h"
#include "../core/Config.h"

#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QUrlQuery>

QString OfficialFallback::dataUrlFromCardcode(const QString &cardcode)
{
    return "https://ws-tcg.com/manage/CardListUser/searchJson?keyword="
           + QString::fromUtf8(QUrl::toPercentEncoding(cardcode));
}

static QString colorFromToken(const QString &s)
{
    QRegularExpression re("\\[\\[(\\w+)\\.gif\\]\\]");
    auto m = re.match(s);
    return m.hasMatch() ? m.captured(1).toUpper() : QString();
}
static int soulCount(const QString &s)
{
    return s.count("soul.gif");
}
static QString kindToCardtype(const QString &k)
{
    if (k == "0")
        return "CX";
    if (k == "1")
        return "EV";
    if (k == "2")
        return "CH";
    return k;
}
static QJsonArray triggerTokens(const QString &s)
{
    QJsonArray out;
    if (s.isEmpty() || s == "-")
        return out;
    QRegularExpression re("\\[\\[(\\w+)\\.gif\\]\\]");
    auto it = re.globalMatch(s);
    while (it.hasNext())
        out.append(it.next().captured(1).toUpper());
    return out;
}

QJsonObject OfficialFallback::reshapeOfficialItem(const QJsonObject &o)
{
    QJsonArray attrs;
    for (const char *f : {"feature1", "feature2", "feature3"}) {
        QString v = o.value(f).toString();
        if (!v.isEmpty())
            attrs.append(v);
    }
    // ability lines from text split on <br>
    QJsonArray ability;
    const QString text = o.value("text").toString();
    for (const QString &line : text.split("<br>")) {
        QString t = line.trimmed();
        if (!t.isEmpty())
            ability.append(t);
    }
    const QString flavor = o.value("flavor").toString();

    QJsonObject np;
    np["name"] = o.value("card_name").toString();
    np["ability"] = ability;
    np["flavor"] = (flavor == "-" || flavor.isEmpty()) ? QJsonValue() : flavor;
    np["attributes"] = attrs;

    const QString cardcode = o.value("card_number").toString();
    const QString setCode = cardcode.section('/', 1).section('-', 0, 0);

    auto toInt = [&](const char *k) { return o.value(k).toString().toInt(); };

    QJsonObject card;
    card["cardcode"] = cardcode;
    card["set"] = setCode;
    card["cardtype"] = kindToCardtype(o.value("card_kind").toString());
    card["colour"] = colorFromToken(o.value("color").toString());
    card["level"] = toInt("level");
    card["cost"] = toInt("cost");
    card["power"] = toInt("power");
    card["soul"] = soulCount(o.value("soul").toString());
    card["rarity"] = o.value("rare").toString();
    card["trigger"] = triggerTokens(o.value("card_trigger").toString());
    card["imagepath"] = o.value("picture").toString();
    QJsonObject locale;
    locale["EN"] = QJsonObject();
    locale["NP"] = np;
    card["locale"] = locale;
    card["_source"] = "official";
    card["_region"] = "JP";
    card["_localeAvailable"] = false;
    return card;
}

//English
namespace {

const QRegularExpression::PatternOptions kOpts = QRegularExpression::DotMatchesEverythingOption
                                                 | QRegularExpression::CaseInsensitiveOption;

QString htmlToText(const QString &fragment)
{
    if (fragment.isEmpty())
        return {};
    return QTextDocumentFragment::fromHtml(fragment).toPlainText().trimmed();
}

QString firstCapture(const QString &html, const QString &pattern)
{
    const QRegularExpression re(pattern, kOpts);
    const auto m = re.match(html);
    return m.hasMatch() ? m.captured(1) : QString();
}

QStringList gifTokens(const QString &html)
{
    static const QRegularExpression
        re(R"re(partimages/([A-Za-z0-9_\-]+)\.(?:gif|png|jpe?g|svg|webp))re",
           QRegularExpression::CaseInsensitiveOption);
    QStringList out;
    auto it = re.globalMatch(html);
    while (it.hasNext())
        out << it.next().captured(1).toLower();
    return out;
}

QString enCardType(const QString &t)
{
    const QString s = t.trimmed().toLower();
    if (s.startsWith("character"))
        return "CH";
    if (s.startsWith("event"))
        return "EV";
    if (s.startsWith("climax"))
        return "CX";
    return t.trimmed();
}

int intOrZero(const QString &s)
{
    bool ok = false;
    const int v = s.trimmed().toInt(&ok);
    return ok ? v : 0;
}

} // namespace

QString OfficialFallback::enPageUrlFromCardcode(const QString &cardcode)
{
    QUrl url(enSiteBaseUrl() + "/cardlist/");
    QUrlQuery q;
    q.addQueryItem("cardno", cardcode);
    url.setQuery(q);
    return url.toString(QUrl::FullyEncoded);
}

QJsonObject OfficialFallback::reshapeEnOfficialHtml(const QByteArray &raw,
                                                    const QString &expectedCardcode)
{
    const QString page = QString::fromUtf8(raw);

    const int start = page.indexOf(QLatin1String("p-cards__detail-wrapper"));
    if (start < 0)
        return {};
    const int end = page.indexOf(QLatin1String("p-cards__detail-copyrights"), start);
    const QString html = (end > start) ? page.mid(start, end - start) : page.mid(start);

    const QString cardcode = htmlToText(firstCapture(html, R"(<p\s+class="number"[^>]*>(.*?)</p>)"));
    if (cardcode.isEmpty())
        return {};
    if (!expectedCardcode.isEmpty() && cardcode.compare(expectedCardcode, Qt::CaseInsensitive) != 0)
        return {};

    const QString name = htmlToText(firstCapture(html, R"(<p\s+class="ttl[^"]*"[^>]*>(.*?)</p>)"));
    const QString imgPath
        = firstCapture(html, R"re(<div\s+class="image"[^>]*>\s*<img[^>]*src="([^"]+)")re");

    QHash<QString, QString> fields;
    {
        static const QRegularExpression dlRe(R"(<dt[^>]*>(.*?)</dt>\s*<dd[^>]*>(.*?)</dd>)", kOpts);
        auto it = dlRe.globalMatch(html);
        while (it.hasNext()) {
            const auto m = it.next();
            fields.insert(htmlToText(m.captured(1)).toLower(), m.captured(2));
        }
    }
    auto fieldText = [&](const char *key) { return htmlToText(fields.value(key)); };

    QJsonArray attrs;
    static const QRegularExpression traitSep(QStringLiteral("[\u30FB\uFF65]"));
    for (const QString &t : fieldText("traits").split(traitSep, Qt::SkipEmptyParts)) {
        const QString s = t.trimmed();
        if (!s.isEmpty() && s != "-")
            attrs.append(s);
    }

    QJsonArray ability;
    {
        const QString abilityHtml
            = firstCapture(html, R"(class="p-cards__detail(?:\s[^"]*)?"\s*>\s*<p[^>]*>(.*?)</p>)");
        static const QRegularExpression brRe(R"(<br\s*/?>)",
                                             QRegularExpression::CaseInsensitiveOption);
        for (const QString &part : abilityHtml.split(brRe)) {
            const QString line = htmlToText(part);
            if (!line.isEmpty() && line != "-")
                ability.append(line);
        }
    }

    QString flavor = htmlToText(
        firstCapture(html, R"(class="p-cards__detail-serif[^"]*"\s*>\s*<p[^>]*>(.*?)</p>)"));
    if (flavor == "-")
        flavor.clear();

    QJsonArray triggers;
    for (const QString &t : gifTokens(fields.value("trigger")))
        triggers.append(t.toUpper());

    const QString colour = gifTokens(fields.value("color")).value(0).toUpper();
    const QString side = gifTokens(fields.value("side")).value(0).toUpper();
    const int soul = gifTokens(fields.value("soul")).count("soul");

    QJsonObject en;
    en["name"] = name;
    en["ability"] = ability;
    en["flavor"] = flavor.isEmpty() ? QJsonValue() : QJsonValue(flavor);
    en["attributes"] = attrs;

    QJsonObject card;
    card["cardcode"] = cardcode;
    card["set"] = cardcode.section('/', 1).section('-', 0, 0);
    card["expansion"] = fieldText("expansion");
    card["side"] = side;
    card["cardtype"] = enCardType(fieldText("card type"));
    card["colour"] = colour;
    card["level"] = intOrZero(fieldText("level"));
    card["cost"] = intOrZero(fieldText("cost"));
    card["power"] = intOrZero(fieldText("power"));
    card["soul"] = soul;
    card["rarity"] = fieldText("rarity");
    card["trigger"] = triggers;
    card["imagepath"] = imgPath;

    QJsonObject locale;
    locale["EN"] = en;
    locale["NP"] = QJsonObject();
    card["locale"] = locale;

    card["_source"] = "official";
    card["_region"] = "EN";
    card["_localeAvailable"] = true;
    return card;
}

// Shared
QString OfficialFallback::resolveImageUrl(const QJsonObject &card)
{
    const QString path = card.value("imagepath").toString();
    if (path.isEmpty())
        return {};

    if (path.startsWith("http://") || path.startsWith("https://"))
        return path;

    if (card.value("_source").toString() == "official") {
        if (card.value("_region").toString() == "EN")
            return QUrl(enSiteBaseUrl()).resolved(QUrl(path)).toString();
        return imageBaseUrl() + path;
    }
    return Config::instance().getImgUrl(path);
}
