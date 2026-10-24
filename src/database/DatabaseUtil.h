#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "../images/CardImageProvider.h"

class DatabaseUtil : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged)

public:
    using QObject::QObject;

    QString locale() const { return locale_; }
    Q_INVOKABLE void setLocale(const QString& loc);

    Q_INVOKABLE QString imageUrlFor(const QString &cardCode) const;
    Q_INVOKABLE QStringList imageUrlsFor(const QString &cardCode) const;
    Q_INVOKABLE QVariantMap cardDataFor(const QString &cardCode) const;
    Q_INVOKABLE void ensureCardData(const QString &cardCode);
    void purgeMissingCards();
    void cleanupExpiredMissing();

    Q_INVOKABLE void toggleLocale() { setLocale(locale_ == "EN" ? "JP" : "EN"); }

    Q_INVOKABLE QVariantList searchCards(const QString &query, int limit = 40) const;

    Q_INVOKABLE int countAdvanced(const QVariantMap &filters) const;
    Q_INVOKABLE QStringList advancedSearchCodes(const QVariantMap &filters) const;

    Q_INVOKABLE QStringList distinctTraits(const QStringList &sets = {}) const;
    Q_INVOKABLE QStringList distinctTriggers(const QStringList &sets = {}) const;

    bool imageRetryBlocked(const QString &url);
    void recordImageFailure(const QString &url, int httpStatus);

    Q_INVOKABLE bool imageUsesOtherRarity(const QString &cardCode) const;
    void reportLoadedImage(const QString &cardCode, const QString &url);
    Q_INVOKABLE QString officialCardPageUrl(const QString &cardCode) const;

signals:
    void localeChanged();
    void cardReady(const QString& cardCode);
    void cardFetchFailed(const QString& cardCode, const QString& reason);
    void missingCardsPurged(int count);

    void imageFallbackChanged(const QString &cardCode);

private:
    QHash<QString, bool> imageFallback_;

    QString locale_ = "EN";
    QNetworkAccessManager nam_;
    bool cardInDb(const QString &cardCode) const;
    bool isKnownMissing(const QString &cardCode) const;
    void markMissing(const QString &cardCode, const QString &reason);
    void ensureMissingTable();
};