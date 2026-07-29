#include "DatabaseWorker.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QJsonArray>


DatabaseWorker::DatabaseWorker(QObject *parent): QObject(parent)
{

}


void DatabaseWorker::flushBatch() {
    if (cardNumberBatch_.isEmpty()) return;

    db_.transaction();

    QSqlQuery query(db_);
    query.prepare("INSERT INTO dataset (card_number, picture) VALUES (?, ?)");
    query.addBindValue(cardNumberBatch_);
    query.addBindValue(pictureBatch_);

    if (!query.execBatch()) {
        qDebug() << "Worker DB batch exec error:" << query.lastError().text();
        db_.rollback();
    } else {
        db_.commit();
        qDebug() << "Successfully inserted batch of" << cardNumberBatch_.size() << "rows.";
    }
    db_.commit();

    cardNumberBatch_.clear();
    pictureBatch_.clear();
}

void DatabaseWorker::initDatabase(bool dropExisting) {
    db_ = QSqlDatabase::addDatabase("QSQLITE", "worker_connection");
    db_.setDatabaseName("dataset_cache.db");

    if (db_.open()) {
        QSqlQuery q(db_);
        q.exec("PRAGMA journal_mode = WAL;");
        q.exec("PRAGMA synchronous = OFF;");

        if (dropExisting) {
            q.exec("DROP TABLE IF EXISTS dataset;");
        }

        q.exec("CREATE TABLE IF NOT EXISTS dataset ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
               "card_number TEXT, "
               "picture TEXT);");

        q.exec("CREATE INDEX IF NOT EXISTS idx_name ON dataset(card_number);");
    }
}

void DatabaseWorker::processChunk(const QByteArray &data) {
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qDebug() << "JSON Parse Error:" << parseError.errorString()
        << "at offset:" << parseError.offset;
        finishProcessing();
        return;
    }

    if (!doc.isArray()) {
        qDebug() << "JSON Error: Expected root element to be an array [...]";
        finishProcessing();
        return;
    }

    QJsonArray jsonArray = doc.array();
    qDebug() << "Found JSON array with" << jsonArray.size() << "items. Importing...";

    for (const QJsonValue &val : jsonArray) {
        if (val.isObject()) {
            QJsonObject obj = val.toObject();

            // ⚠️ Make sure key names match your JSON file exactly!
            QString cardNumber = obj.value("card_number").toString();
            QString picture = obj.value("picture").toString();

            if (!cardNumber.isEmpty()) {
                cardNumberBatch_.append(cardNumber);
                pictureBatch_.append(picture);
            }

            if (cardNumberBatch_.size() >= BATCH_SIZE) {
                flushBatch();
            }
        }
    }

    flushBatch();
    finishProcessing();
}

void DatabaseWorker::finishProcessing() {
    flushBatch();
    QString connName = db_.connectionName();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
    emit finished();
}