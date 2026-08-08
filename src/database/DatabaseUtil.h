#pragma once
#include <QString>
#include <QSqlDatabase>
#include <QVariantMap>
#include <QObject>

class DatabaseUtil : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged)
public:
    using QObject::QObject;

    QString locale() const { return locale_; }
    Q_INVOKABLE void setLocale(const QString& loc);

    Q_INVOKABLE QString imageUrlFor(const QString &cardCode) const;
    Q_INVOKABLE QVariantMap cardDataFor(const QString &cardCode) const;

    Q_INVOKABLE void toggleLocale() { setLocale(locale_ == "EN" ? "JP" : "EN"); }

signals:
    void localeChanged();

private:
    QString locale_ = "EN";
};