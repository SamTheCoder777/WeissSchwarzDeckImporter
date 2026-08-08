#pragma once

#include <QString>
#include <QSqlDatabase>
#include <QVariantMap>
#include <QObject>
#include <QNetworkAccessManager>

class DatabaseUtil : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged)
public:
    using QObject::QObject;

    QString locale() const { return locale_; }
    Q_INVOKABLE void setLocale(const QString& loc);

    Q_INVOKABLE QString imageUrlFor(const QString &cardCode) const;
    Q_INVOKABLE QVariantMap cardDataFor(const QString &cardCode) const;
    Q_INVOKABLE void ensureCardData(const QString& cardCode);

    Q_INVOKABLE void toggleLocale() { setLocale(locale_ == "EN" ? "JP" : "EN"); }

signals:
    void localeChanged();
    void cardReady(const QString& cardCode);

private:
    QString locale_ = "EN";
    QNetworkAccessManager nam_;
    bool cardInDb(const QString& cardCode) const;
    void storeOfficialCard(const QString& cardCode, const QJsonObject& encoreShaped);
};