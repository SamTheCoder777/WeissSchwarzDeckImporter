#pragma once
#include <QObject>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>

class QProgressDialog;

class AppUpdateController : public QObject {
    Q_OBJECT
public:
    static bool allowStartup();
    explicit AppUpdateController(QObject *parent = nullptr);
    ~AppUpdateController() override;
    bool isInstalling() const { return installing_; }
    void check(bool manual = false);
    void install();

signals:
    void releaseDetected(bool available, const QString &tag,
                         const QString &releaseUrl, const QDateTime &published);
    void actionTextChanged(const QString &text);

private:
    QString repo() const;
    QString manifestUrl() const;
    QString releasePage() const;
    bool decode(const QByteArray &bytes, QJsonObject *manifest,
                QString *error) const;
    void showSavedRelease();
    QString manualReason(const QJsonObject &manifest) const;
    void manualFallback(const QString &reason);
    void pollUpdater();
    bool writeControlFile(const QString &name, const QByteArray &contents);
    void cancelPreparation();
    void preparationFailed(const QString &message);
    void scheduleCheck(qint64 deadline);

    QByteArray manifestBytes_;
    QJsonObject manifest_;
    bool checking_ = false;
    bool installing_ = false;
    bool quitRequested_ = false;
    bool recoveryNeeded_ = false;
    QString session_;
    QString token_;
    QPointer<QProgressDialog> progress_;
    QTimer poll_;
    QTimer autoCheck_;
    QElapsedTimer elapsed_;
    qint64 lastSequence_ = -1;
    qint64 lastHeartbeat_ = 0;
};
