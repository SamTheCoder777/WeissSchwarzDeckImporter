#pragma once

#include <QObject>
#include <QSqlDatabase>



class DatabaseWorker : public QObject {
    Q_OBJECT
private:
    QSqlDatabase db_;
    QVariantList cardNumberBatch_;
    QVariantList pictureBatch_;
    const int BATCH_SIZE = 5000;

    void flushBatch();

public:
    explicit DatabaseWorker(QObject *parent = nullptr);

public slots:
    void initDatabase(bool dropExisting);
    void processChunk(const QByteArray &data);
    void finishProcessing();

signals:
    void finished();

};


