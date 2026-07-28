// carddatabase.h
#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QNetworkReply>
#include <QFutureWatcher>
#include <QtConcurrent>

class CardDatabase : public QObject {
    Q_OBJECT
public:
    explicit CardDatabase(QObject* parent = nullptr) : QObject(parent) {}

    void load(const QUrl& jsonUrl) {
        auto* reply = nam_.get(QNetworkRequest(jsonUrl));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                qWarning() << "card db fetch failed:" << reply->errorString();
                emit loadFailed(reply->errorString());
                return;
            }

            const QByteArray raw = reply->readAll();

            auto* watcher = new QFutureWatcher<QHash<QString, QString>>(this);
            connect(watcher, &QFutureWatcher<QHash<QString, QString>>::finished, this, [this, watcher](){
                pictureByCardId_ = watcher->result();
                loaded_ = true;
                emit loaded();
                watcher->deleteLater();
            });

            watcher->setFuture(QtConcurrent::run([this, raw](){
                return CardDatabase::parse(raw);
            }));
        });
    }

    QString imageUrlFor(const QString& cardId) const {
        auto it = pictureByCardId_.constFind(cardId);
        return it == pictureByCardId_.constEnd() ? QString() : baseImgUrl_ + it.value();
    }

    bool isLoaded() const { return loaded_; }

signals:
    void loaded();
    void loadFailed(const QString& error);

private:
    QHash<QString, QString> parse(const QByteArray& data) {
        const auto arr = QJsonDocument::fromJson(data).array();
        QHash<QString, QString> result;
        for (const auto& v : arr) {
            const auto obj = v.toObject();
            const QString cardNumber = obj.value("card_number").toString();
            const QString picture    = obj.value("picture").toString();
            if (!cardNumber.isEmpty())
                result.insert(cardNumber, picture);
        }
        return result;
    }

    QNetworkAccessManager nam_;
    QHash<QString, QString> pictureByCardId_;
    QString baseImgUrl_ = "https://ws-tcg.com/wordpress/wp-content/images/cardlist/";
    bool loaded_ = false;
};