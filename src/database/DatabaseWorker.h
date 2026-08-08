#pragma once

#include <QObject>
#include <QSqlDatabase>



class DatabaseWorker : public QObject {
    Q_OBJECT
private:
    QSqlDatabase seriesListDb_;
    QSqlDatabase cardListDb_;

    QSqlDatabase* curDb_ = nullptr;

    QVariantList cardNumberBatch_;
    QVariantList pictureBatch_;
    const int BATCH_SIZE = 5000;

    void flushBatch();

public:
    explicit DatabaseWorker(QObject *parent = nullptr);
    enum class DatabaseMode{seriesList, cardList};

public slots:
    void setMode(DatabaseWorker::DatabaseMode mode);
    void initSerieslistDatabase(bool dropExisting);
    void initCardListDatabase(bool dropExisting);
    void processChunk(const QByteArray &data);
    void finishProcessing();

signals:
    void finished();
    void statusChanged(const QString &status);

};


