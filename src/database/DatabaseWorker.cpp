#include "DatabaseWorker.h"
#include "../core/Config.h"

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
    db_.setDatabaseName(Config::instance().getDatasetPath());

    if (db_.open()) {
        QSqlQuery q(db_);
        q.exec("PRAGMA journal_mode = WAL;");
        q.exec("PRAGMA synchronous = OFF;");

        if (dropExisting) {
            q.exec("DROP TABLE IF EXISTS dataset;");
        }
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

    if (jsonArray.isEmpty()) {
        finishProcessing();
        return;
    }

    QSet<QString> keySet;
    for (const QJsonValue &val : jsonArray) {
        if (val.isObject()) {
            QJsonObject obj = val.toObject();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                keySet.insert(it.key());
            }
        }
    }

    QStringList rawKeys = QStringList(keySet.begin(), keySet.end());
    if (rawKeys.isEmpty()) {
        finishProcessing();
        return;
    }


    QStringList rawKeysCleaned;
    QStringList placeholders;
    QStringList columnDefs;
    QStringList quotedColumns;

    columnDefs.append("_db_id INTEGER PRIMARY KEY AUTOINCREMENT");

    for (const QString &key : rawKeys) {
        QString clean = key;
        clean.replace(" ", "_").replace(".", "_").replace("-", "_");

        rawKeysCleaned.append(clean);
        placeholders.append(":" + clean);
        quotedColumns.append(QString("\"%1\"").arg(clean));
        columnDefs.append(QString("\"%1\" TEXT").arg(clean));
    }

    QSqlQuery q(db_);

    q.exec("DROP TABLE IF EXISTS dataset;");

    QString createTableSql = QString("CREATE TABLE dataset (%1);").arg(columnDefs.join(", "));
    if (!q.exec(createTableSql)) {
        qDebug() << "Failed to create table:" << q.lastError().text();
        qDebug() << "Executed SQL was:" << createTableSql;
        finishProcessing();
        return;
    }

    if (rawKeysCleaned.contains("card_number")) {
        q.exec("CREATE INDEX IF NOT EXISTS idx_name ON dataset(\"card_number\");");
    }

    QString insertSql = QString("INSERT INTO dataset (%1) VALUES (%2)")
                            .arg(quotedColumns.join(", "))
                            .arg(placeholders.join(", "));

    if (!q.prepare(insertSql)) {
        qDebug() << "Failed to prepare insert query:" << q.lastError().text();
        qDebug() << "Executed SQL was:" << insertSql;
        finishProcessing();
        return;
    }

    db_.transaction();

    int count = 0;
    const int BATCH_SIZE = 10000;

    for (const QJsonValue &val : jsonArray) {
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();

        for (int i = 0; i < rawKeys.size(); ++i) {
            const QString &rawKey = rawKeys[i];
            const QString &placeholder = placeholders[i];
            QJsonValue jVal = obj.value(rawKey);

            if (jVal.isObject() || jVal.isArray()) {
                QJsonDocument subDoc = jVal.isArray() ? QJsonDocument(jVal.toArray()) : QJsonDocument(jVal.toObject());
                q.bindValue(placeholder, QString(subDoc.toJson(QJsonDocument::Compact)));
            } else if (jVal.isNull() || jVal.isUndefined()) {
                q.bindValue(placeholder, QVariant(QVariant::String));
            } else {
                q.bindValue(placeholder, jVal.toVariant().toString());
            }
        }

        if (!q.exec()) {
            qDebug() << "Insert error:" << q.lastError().text();
        }

        count++;

        if (count % BATCH_SIZE == 0) {
            db_.commit();
            db_.transaction();
        }
    }

    db_.commit();
    qDebug() << "Successfully imported" << count << "records into dataset table!";

    finishProcessing();
}
void DatabaseWorker::finishProcessing() {
    flushBatch();
    QString connName = db_.connectionName();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connName);
    emit finished();
}