#include "AppUpdateController.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>
#include <QVersionNumber>
#include <QWidget>

#include "../network/NetGuard.h"
#include "Config.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
QVersionNumber numericVersion(const QString &text)
{
    const auto match = QRegularExpression(QStringLiteral("^v?(\\d+\\.\\d+\\.\\d+(?:\\.\\d+)?)$"))
                           .match(text);
    return match.hasMatch() ? QVersionNumber::fromString(match.captured(1)).normalized()
                            : QVersionNumber();
}
bool trustedReleaseUrl(const QString &url, const QString &repo)
{
    const QUrl parsed(url);
    const QString prefix = "/" + repo + "/releases";
    return parsed.isValid() && parsed.scheme() == "https"
           && parsed.host().compare("github.com", Qt::CaseInsensitive) == 0
           && parsed.userInfo().isEmpty() && parsed.port(443) == 443
           && (parsed.path() == prefix || parsed.path().startsWith(prefix + "/"));
}

} // namespace

bool AppUpdateController::allowStartup()
{
#ifdef Q_OS_WIN
    wchar_t executableBuffer[32768];
    const DWORD length = GetModuleFileNameW(nullptr, executableBuffer, 32768);
    if (length == 0 || length >= 32768)
        return false;
    const QFileInfo executable(QString::fromWCharArray(executableBuffer, int(length)));
    const QString installDir = executable.absolutePath();
    const QString lockPath = QDir::toNativeSeparators(installDir + "/.wsdeck-update/lock");
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(lockPath.utf16()),
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    const DWORD error = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
    if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) {
        MessageBoxW(nullptr,
                    L"An update is being prepared or installed. Please wait for it to finish.",
                    L"WSDeckImporter update",
                    MB_OK | MB_ICONINFORMATION);
        return false;
    }
    QFile journal(installDir + "/.wsdeck-update/journal.json");
    if (!journal.exists())
        return true;
    if (!journal.open(QIODevice::ReadOnly) || journal.size() > 8 * 1024 * 1024) {
        MessageBoxW(nullptr,
                    L"Update recovery metadata could not be read. Keep the .wsdeck-update folder "
                    L"and reinstall manually.",
                    L"WSDeckImporter update",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    const auto metadata = QJsonDocument::fromJson(journal.readAll()).object();
    journal.close();
    if (metadata.value("phase").toString() == "committed")
        return true;
    const QString repository = metadata.value("repo").toString();
    if (metadata.value("phase").toString() != "applying"
        || !QRegularExpression("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$").match(repository).hasMatch()
        || metadata.value("exe").toString().compare(executable.fileName(), Qt::CaseInsensitive)
               != 0) {
        MessageBoxW(nullptr,
                    L"An interrupted update has invalid recovery metadata. Keep the backups and "
                    L"reinstall manually.",
                    L"WSDeckImporter update",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    QTemporaryDir temporary(QDir::tempPath() + "/wsdeck-recovery-XXXXXX");
    bool launched = false;
    if (temporary.isValid()
        && QFile::copy(installDir + "/updater.exe", temporary.path() + "/updater.exe")) {
        const QStringList arguments{"--install-dir",
                                    QDir::toNativeSeparators(installDir),
                                    "--exe",
                                    executable.fileName(),
                                    "--pid",
                                    "0",
                                    "--repo",
                                    repository,
                                    "--release-url",
                                    "https://github.com/" + repository + "/releases",
                                    "--session-dir",
                                    QDir::toNativeSeparators(temporary.path()),
                                    "--token",
                                    QUuid::createUuid().toString(QUuid::Id128),
                                    "--recover",
                                    "true",
                                    "--standalone-recover",
                                    "true"};
        launched = QProcess::startDetached(temporary.path() + "/updater.exe",
                                           arguments,
                                           temporary.path());
        if (launched)
            temporary.setAutoRemove(false);
    }
    if (!launched)
        MessageBoxW(nullptr,
                    L"The previous update was interrupted and recovery could not start. Keep "
                    L".wsdeck-update and reinstall manually.",
                    L"WSDeckImporter update",
                    MB_OK | MB_ICONERROR);
    return false;
#else
    return true;
#endif
}

AppUpdateController::AppUpdateController(QObject *parent)
    : QObject(parent)
{
    poll_.setInterval(250);
    connect(&poll_, &QTimer::timeout, this, &AppUpdateController::pollUpdater);
    autoCheck_.setSingleShot(true);
    autoCheck_.setTimerType(Qt::PreciseTimer);
    connect(&autoCheck_, &QTimer::timeout, this, [this] { check(false); });
}

AppUpdateController::~AppUpdateController()
{
    if (installing_ && !quitRequested_)
        writeControlFile("cancel", token_.toUtf8());
}

QString AppUpdateController::repo() const
{
    return Config::instance().releaseRepo_;
}
QString AppUpdateController::manifestUrl() const
{
    return "https://github.com/" + repo() + "/releases/latest/download/versions.json";
}
QString AppUpdateController::releasePage() const
{
    const QString selectedUrl = manifest_.value("release_url").toString();
    return trustedReleaseUrl(selectedUrl, repo()) ? selectedUrl
                                                  : "https://github.com/" + repo() + "/releases";
}

bool AppUpdateController::decode(const QByteArray &bytes,
                                 QJsonObject *manifest,
                                 QString *error) const
{
    if (bytes.isEmpty() || bytes.size() > 2 * 1024 * 1024) {
        *error = "Manifest is empty or too large.";
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = "versions.json must contain a valid JSON object.";
        return false;
    }
    *manifest = document.object();
    if (numericVersion(manifest->value("version").toString()).isNull()
        || !trustedReleaseUrl(manifest->value("release_url").toString(), repo())) {
        *error = "The release metadata is invalid.";
        return false;
    }
    return true;
}

void AppUpdateController::showSavedRelease()
{
    if (manifestBytes_.isEmpty()) {
        manifestBytes_
            = Config::instance().appUpdaterState(repo()).value("manifestJson").toByteArray();
    }
    QString error;
    QJsonObject manifest;
    if (!manifestBytes_.isEmpty() && decode(manifestBytes_, &manifest, &error)) {
        manifest_ = manifest;
        const QString tag = manifest.value("version").toString();
        const auto current = numericVersion(QStringLiteral(APP_VERSION));
        const bool newer = !current.isNull() && numericVersion(tag) > current;
        const bool dismissed = Config::instance().getDismissedReleaseTag() == tag;
        emit actionTextChanged(manualReason(manifest).isEmpty() ? "Update now" : "Manual update");
        emit releaseDetected(newer && !dismissed,
                             tag,
                             releasePage(),
                             QDateTime::fromString(manifest.value("published_at").toString(),
                                                   Qt::ISODate));
    } else {
        emit releaseDetected(false, {}, releasePage(), {});
    }

    QFile journal(QCoreApplication::applicationDirPath() + "/.wsdeck-update/journal.json");
    recoveryNeeded_ = false;
    if (journal.open(QIODevice::ReadOnly) && journal.size() <= 8 * 1024 * 1024) {
        const auto object = QJsonDocument::fromJson(journal.readAll()).object();
        recoveryNeeded_ = object.value("phase").toString() == "applying";
    }
    if (recoveryNeeded_) {
        emit actionTextChanged("Repair interrupted update");
        emit releaseDetected(true, "interrupted update", releasePage(), {});
    }
}

QString AppUpdateController::manualReason(const QJsonObject &manifest) const
{
#ifndef Q_OS_WIN
    return "Automatic installation is available only on Windows.";
#else
#ifndef Q_PROCESSOR_X86_64
    return "This updater package supports x64 builds only.";
#endif
    if (manifest.value("manual").toBool())
        return "This release requires manual installation.";
    if (manifest.value("schema").toInt() != 1)
        return "This release uses an unsupported update format.";
    const auto minimum = numericVersion(manifest.value("auto_update_from").toString());
    const auto current = numericVersion(QStringLiteral(APP_VERSION));
    if (minimum.isNull() || current.isNull() || current < minimum)
        return "This installed version must be upgraded manually.";
    if (manifest.value("architecture").toString() != "win-x64" || sizeof(void *) != 8)
        return "The update does not match this application's architecture.";
    if (manifest.value("exe").toString().compare(QFileInfo(QCoreApplication::applicationFilePath())
                                                     .fileName(),
                                                 Qt::CaseInsensitive)
        != 0)
        return "The update targets a different application executable.";
    if (!QFileInfo::exists(QCoreApplication::applicationDirPath() + "/updater.exe"))
        return "updater.exe is missing from the application folder.";
    return {};
#endif
}

void AppUpdateController::check(bool manual)
{
    if (checking_ || installing_)
        return;
    if (!QRegularExpression("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$").match(repo()).hasMatch()) {
        if (manual)
            manualFallback("The release repository is not configured correctly.");
        return;
    }
    showSavedRelease();
    QVariantMap saved = Config::instance().appUpdaterState(repo());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 next = saved.value("nextAllowed").toLongLong();

    if (next > now + 7 * 24 * 3600)
        next = now + 3600;
    if (next > now) {
        scheduleCheck(next);
        if (manual)
            QMessageBox::information(qobject_cast<QWidget *>(parent()),
                                     "Update check",
                                     QString("The next app update check is allowed in %1 "
                                             "minute(s). Saved update information is shown.")
                                         .arg((next - now + 59) / 60));
        return;
    }
    checking_ = true;
    const QString repository = repo();
    NetRequest request;
    request.url = QUrl(manifestUrl());
    request.key = "app-manifest:" + repository;
    request.useCache = false;
    request.timeoutMs = 15000;
    request.maxBodyBytes = 2 * 1024 * 1024;
    request.retries = 0;
    request.onStarted = [this, repository] {
        auto state = Config::instance().appUpdaterState(repository);
        state.insert("nextAllowed", QDateTime::currentSecsSinceEpoch() + 3600);
        Config::instance().setAppUpdaterState(repository, state);
        scheduleCheck(state.value("nextAllowed").toLongLong());
    };
    NetGuard::instance().get(request, this, [this, repository, manual](const NetResult &result) {
        checking_ = false;
        auto saved = Config::instance().appUpdaterState(repository);
        if (result.retryInMs > 0) {
            const qint64 serverNext = QDateTime::currentSecsSinceEpoch() + result.retryInMs / 1000
                                      + (result.retryInMs % 1000 != 0);
            saved.insert("nextAllowed", qMax(saved.value("nextAllowed").toLongLong(), serverNext));
            Config::instance().setAppUpdaterState(repository, saved);
        }
        QString error = result.message;
        QJsonObject manifest;
        if (result.ok() && decode(result.body, &manifest, &error)) {
            saved.insert("manifestJson", result.body);
            Config::instance().setAppUpdaterState(repository, saved);
            manifestBytes_ = result.body;
            showSavedRelease();
            if (manual
                && numericVersion(manifest.value("version").toString())
                       <= numericVersion(QStringLiteral(APP_VERSION)))
                QMessageBox::information(qobject_cast<QWidget *>(parent()),
                                         "Update check",
                                         "This application is up to date.");
        } else {
            showSavedRelease();
            qWarning() << "[AppUpdate]" << error;
            if (manual)
                manualFallback("Could not verify current release information.\n" + error);
        }
        scheduleCheck(saved.value("nextAllowed").toLongLong());
    });
}

void AppUpdateController::scheduleCheck(qint64 deadline)
{
    const qint64 seconds = qMax(qint64{1}, deadline - QDateTime::currentSecsSinceEpoch());
    const qint64 milliseconds = qMin(qint64{24 * 60 * 60 * 1000}, seconds * 1000 + 1000);
    autoCheck_.start(int(milliseconds));
}

void AppUpdateController::manualFallback(const QString &reason)
{
    QMessageBox message(qobject_cast<QWidget *>(parent()));
    message.setWindowTitle("WSDeckImporter update");
    message.setIcon(QMessageBox::Warning);
    message.setText(reason);
    message.setInformativeText(
        "You can download the release manually. Your application will remain open.");
    auto *open = message.addButton("Open release page", QMessageBox::ActionRole);
    message.addButton(QMessageBox::Close);
    message.exec();
    if (message.clickedButton() == open)
        QDesktopServices::openUrl(QUrl(releasePage()));
}

void AppUpdateController::install()
{
    if (installing_)
        return;
    if (checking_) {
        QMessageBox::information(qobject_cast<QWidget *>(parent()),
                                 "Update check",
                                 "Wait for the current check to finish.");
        return;
    }
    showSavedRelease();
    if (!recoveryNeeded_) {
        if (manifest_.isEmpty()) {
            manualFallback("No verified update manifest is available.");
            return;
        }
        if (numericVersion(manifest_.value("version").toString())
            <= numericVersion(QStringLiteral(APP_VERSION))) {
            QMessageBox::information(qobject_cast<QWidget *>(parent()),
                                     "Update",
                                     "No newer release is selected.");
            return;
        }
        const QString reason = manualReason(manifest_);
        if (!reason.isEmpty()) {
            manualFallback(reason);
            return;
        }
    }
    if (QMessageBox::question(
            qobject_cast<QWidget *>(parent()),
            "WSDeckImporter update",
            "The updater will prepare and verify the package while this app stays open. "
            "It will then close and restart the app. Save any work before continuing.")
        != QMessageBox::Yes)
        return;
    const QString installDir = QCoreApplication::applicationDirPath();
    const QString executable = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    QTemporaryDir temporary(QDir::tempPath() + "/wsdeck-update-XXXXXX");
    if (!temporary.isValid()) {
        manualFallback("Could not create an updater session directory.");
        return;
    }
    session_ = temporary.path();
    token_ = QUuid::createUuid().toString(QUuid::Id128);
    const QString updater = session_ + "/updater.exe";
    if (!QFile::copy(installDir + "/updater.exe", updater)) {
        manualFallback("Could not copy updater.exe to the temporary directory.");
        return;
    }
    QStringList arguments{"--install-dir",
                          QDir::toNativeSeparators(installDir),
                          "--exe",
                          executable,
                          "--pid",
                          QString::number(QCoreApplication::applicationPid()),
                          "--repo",
                          repo(),
                          "--manifest-url",
                          manifestUrl(),
                          "--release-url",
                          releasePage(),
                          "--current-version",
                          QStringLiteral(APP_VERSION),
                          "--expected-manifest-sha256",
                          QString::fromLatin1(
                              QCryptographicHash::hash(manifestBytes_, QCryptographicHash::Sha256)
                                  .toHex()),
                          "--session-dir",
                          QDir::toNativeSeparators(session_),
                          "--token",
                          token_};
    if (recoveryNeeded_)
        arguments << "--recover" << "true";
    qint64 processId = 0;
    if (!QProcess::startDetached(updater, arguments, session_, &processId)) {
        manualFallback("Windows could not start updater.exe. The app has not been closed.");
        return;
    }
    temporary.setAutoRemove(false);
    installing_ = true;
    quitRequested_ = false;
    lastSequence_ = -1;
    lastHeartbeat_ = 0;
    elapsed_.start();
    progress_ = new QProgressDialog("Starting updater…",
                                    "Cancel",
                                    0,
                                    100,
                                    qobject_cast<QWidget *>(parent()));
    progress_->setAttribute(Qt::WA_DeleteOnClose);
    progress_->setWindowTitle("WSDeckImporter update");
    progress_->setWindowModality(Qt::WindowModal);
    progress_->setAutoClose(false);
    progress_->setAutoReset(false);
    progress_->setMinimumDuration(0);
    connect(progress_, &QProgressDialog::canceled, this, &AppUpdateController::cancelPreparation);
    progress_->show();
    poll_.start();
}

bool AppUpdateController::writeControlFile(const QString &name, const QByteArray &contents)
{
    if (session_.isEmpty())
        return false;
    QSaveFile file(session_ + "/" + name);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(contents) != contents.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}
void AppUpdateController::cancelPreparation()
{
    if (quitRequested_)
        return;
    writeControlFile("cancel", token_.toUtf8());
    poll_.stop();
    installing_ = false;
    if (progress_) {
        progress_->close();
        progress_ = nullptr;
    }
}
void AppUpdateController::preparationFailed(const QString &message)
{
    writeControlFile("cancel", token_.toUtf8());
    poll_.stop();
    installing_ = false;
    if (progress_) {
        progress_->close();
        progress_ = nullptr;
    }
    manualFallback(message
                   + "\n\nUpdater log: " + QDir::toNativeSeparators(session_ + "/updater.log"));
}
void AppUpdateController::pollUpdater()
{
    QFile file(session_ + "/status.json");
    if (file.open(QIODevice::ReadOnly) && file.size() < 1024 * 1024) {
        const auto object = QJsonDocument::fromJson(file.readAll()).object();
        if (object.value("token").toString() == token_) {
            const qint64 sequence = qint64(object.value("sequence").toDouble());
            if (sequence != lastSequence_) {
                lastSequence_ = sequence;
                lastHeartbeat_ = elapsed_.elapsed();
            }
            const QString phase = object.value("phase").toString();
            if (progress_) {
                progress_->setLabelText(object.value("message").toString());
                progress_->setValue(object.value("progress").toInt());
            }
            if (phase == "ready" && !quitRequested_) {
                if (!writeControlFile("close.ack", token_.toUtf8())) {
                    preparationFailed("Could not acknowledge the prepared update.");
                    return;
                }
                quitRequested_ = true;
                if (progress_)
                    progress_->setCancelButton(nullptr);
                QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
                return;
            }
            if (phase == "error") {
                poll_.stop();
                installing_ = false;
                quitRequested_ = false;
                if (progress_) {
                    progress_->close();
                    progress_ = nullptr;
                }
                return;
            }
            if (phase == "done" || phase == "recovered") {
                poll_.stop();
                installing_ = false;
                if (progress_) {
                    progress_->close();
                    progress_ = nullptr;
                }
                return;
            }
        }
    }
    const qint64 silence = elapsed_.elapsed() - lastHeartbeat_;
    if ((lastSequence_ < 0 && elapsed_.elapsed() > 20000) || silence > 120000)
        preparationFailed("The updater did not respond. The app will remain open. "
                          "Check that .NET Framework 4.8 is installed.");
}
