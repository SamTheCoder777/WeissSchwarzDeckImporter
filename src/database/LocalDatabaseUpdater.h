#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <memory>

class QNetworkReply;
class QTemporaryFile;
class QCryptographicHash;

class LocalDatabaseUpdater final : public QObject
{
    Q_OBJECT
public:
    explicit LocalDatabaseUpdater(QObject *parent = nullptr);
    ~LocalDatabaseUpdater() override;

    static void runBeforeMainWindow();

signals:
    void progress(const QString &message);
    void finished(const QString &error);

private:
    void start();
    void download();
    void consumeDownload();
    void finish(const QString &error = QString());
    static bool checkDue();
    static QString destination();

    bool done_ = false;
    quint64 manifestRequest_ = 0;
    QUrl downloadUrl_;
    QString expectedHash_;
    QString downloadError_;
    QNetworkReply *reply_ = nullptr;
    std::unique_ptr<QTemporaryFile> zip_;
    std::unique_ptr<QCryptographicHash> zipHash_;
    QTimer downloadDeadline_;
    QFutureWatcher<QString> installer_;
};
