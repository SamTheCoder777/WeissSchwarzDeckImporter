#pragma once
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace ReferenceSearch {
inline QString compactName(QString value)
{
    value = value.trimmed();
    if (value.startsWith(QStringLiteral("「")) && value.endsWith(QStringLiteral("」")))
        value = value.mid(1, value.size() - 2);
    static const QRegularExpression spaces(QStringLiteral("[\\s\u00a0\u3000]+"),
        QRegularExpression::UseUnicodePropertiesOption);
    value.remove(spaces);
    return value;
}
inline bool matchesName(const QString &term, const QString &text)
{
    return !term.isEmpty() && compactName(text).contains(term, Qt::CaseInsensitive);
}
inline QString normalizedText(QString value)
{
    static const QRegularExpression br(QStringLiteral(R"(<[/\\]?\s*br\s*[/\\]?>)"),
                                       QRegularExpression::CaseInsensitiveOption);
    value.replace(br, QStringLiteral(" "));
    value.replace(QStringLiteral("【"), QStringLiteral(" "));
    value.replace(QStringLiteral("】"), QStringLiteral(" "));
    value.replace(QStringLiteral("\u00a0"), QStringLiteral(" "));
    value.replace(QStringLiteral("\u3000"), QStringLiteral(" "));
    return value;
}
inline QRegularExpression abilityPattern(QString term)
{
    term = term.trimmed();
    if (term.startsWith(QStringLiteral("『")) && term.endsWith(QStringLiteral("』")))
        term = term.mid(1, term.size() - 2);
    term = normalizedText(term).trimmed();
    if (term.isEmpty()) return QRegularExpression(QStringLiteral("(?!)"));
    static const QRegularExpression tokens(QStringLiteral(R"([A-Za-z_]+|[0-9]+|[^\s])"));
    static const QRegularExpression firstWord(QStringLiteral("^[A-Za-z0-9_]"));
    static const QRegularExpression lastWord(QStringLiteral("[A-Za-z0-9_]$"));
    QStringList pieces;
    auto matches = tokens.globalMatch(term);
    while (matches.hasNext()) pieces << QRegularExpression::escape(matches.next().captured(0));
    if (pieces.isEmpty()) return QRegularExpression(QStringLiteral("(?!)"));
    const QString before = firstWord.match(term).hasMatch() ? QStringLiteral("(^|[^A-Za-z0-9_])") : QString();
    const QString after = lastWord.match(term).hasMatch() ? QStringLiteral("(?=$|[^A-Za-z0-9_])") : QString();
    return QRegularExpression(before + pieces.join(QStringLiteral("\\s*")) + after,
                              QRegularExpression::CaseInsensitiveOption);
}
inline bool matchesAbility(const QRegularExpression &pattern, const QString &text)
{
    return pattern.match(normalizedText(text)).hasMatch();
}
} // namespace ReferenceSearch
