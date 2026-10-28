#include "Config.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSettings>

Config &Config::instance()
{
    static Config instance;
    return instance;
}

Config::Config() {
    qRegisterMetaType<QMap<QString, QString>>("QMap<QString,QString>");

    QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    settings_ = std::make_unique<QSettings>(configPath, QSettings::IniFormat);

    load();
}

void Config::load() {
    settings_->beginGroup("Update");
    dismissedReleaseTag_ = settings_->value("DismissedReleaseTag").toString();
    settings_->endGroup();

    settings_->beginGroup("Path");
    curModelPath_ = settings_->value("ModelPath").toString();
    curYoloModelPath_ = settings_->value("YoloModelPath").toString();
    curIndexId_ = settings_->value("IndexId").toString();
    settings_->endGroup();


    settings_->beginGroup("Dataset");
    cardListEtag_ = settings_->value("CardListEtag").value<QMap<QString, QString>>();
    jpSeriesListEtag_ = settings_->value("jpSeriesListEtag").toString();
    enCardListEtag_ = settings_->value("enCardListEtag").value<QMap<QString, QString>>();
    enSeriesListEtag_ = settings_->value("enSeriesListEtag").toString();
    settings_->endGroup();

    settings_->beginGroup("Index");
    disableNameCheck_ = settings_->value("DisableNameCheck").toBool();
    settings_->endGroup();

    settings_->beginGroup("Locale");
    curDetectLocaleMode_ = static_cast<Config::DetectLocaleMode>(
        settings_->value("CurDetectLocaleMode", static_cast<int>(Config::DetectLocaleMode::JP))
            .toInt());
    settings_->endGroup();

    settings_->beginGroup("MissingCards");
    missingPurgeInterval_
        = settings_->value("MissingPurgeInterval", (int) MissingPurgeInterval::Never).toInt();
    settings_->endGroup();

    settings_->beginGroup("Acceleration");
    useAcceleration_ = settings_->value("UseAcceleration", true).toBool();
    settings_->endGroup();

    settings_->beginGroup("UpdateChecks");
    automaticUpdateCheckIntervalMinutes_
        = qMax(1, settings_->value("AutomaticIntervalMinutes", 180).toInt());
    manualUpdateCheckIntervalMinutes_ = qMax(1,
                                             settings_->value("ManualIntervalMinutes", 15).toInt());
    updateCheckTimes_ = settings_->value("LastCheckTimes").toMap();
    settings_->endGroup();

    // Normalize future timestamps after a wall-clock correction across sessions.
    const qint64 updateCheckNow = QDateTime::currentSecsSinceEpoch();
    for (auto check = updateCheckTimes_.begin(); check != updateCheckTimes_.end(); ++check) {
        if (check.value().toLongLong() > updateCheckNow)
            check.value() = updateCheckNow;
    }

    settings_->beginGroup("AppUpdater");
    appUpdaterStates_ = settings_->value("States").toMap();
    settings_->endGroup();
}

void Config::save() {
    settings_->beginGroup("Update");
    settings_->setValue("DismissedReleaseTag", dismissedReleaseTag_);
    settings_->endGroup();

    settings_->beginGroup("Path");
    settings_->setValue("ModelPath", curModelPath_);
    settings_->setValue("YoloModelPath", curYoloModelPath_);
    settings_->setValue("IndexId", curIndexId_);
    settings_->endGroup();

    settings_->beginGroup("Dataset");
    settings_->setValue("CardListEtag", QVariant::fromValue(cardListEtag_));
    settings_->setValue("jpSeriesListEtag", jpSeriesListEtag_);
    settings_->setValue("enCardListEtag", QVariant::fromValue(enCardListEtag_));
    settings_->setValue("enSeriesListEtag", enSeriesListEtag_);
    settings_->endGroup();

    settings_->beginGroup("Index");
    settings_->setValue("DisableNameCheck", disableNameCheck_);
    settings_->endGroup();

    settings_->beginGroup("Locale");
    settings_->setValue("CurDetectLocaleMode", static_cast<int>(curDetectLocaleMode_));
    settings_->endGroup();

    settings_->beginGroup("MissingCards");
    settings_->setValue("MissingPurgeInterval", missingPurgeInterval_);
    settings_->endGroup();

    settings_->beginGroup("Acceleration");
    settings_->setValue("UseAcceleration", useAcceleration_);
    settings_->endGroup();

    settings_->beginGroup("UpdateChecks");
    settings_->setValue("AutomaticIntervalMinutes", automaticUpdateCheckIntervalMinutes_);
    settings_->setValue("ManualIntervalMinutes", manualUpdateCheckIntervalMinutes_);
    settings_->setValue("LastCheckTimes", updateCheckTimes_);
    settings_->endGroup();

    settings_->beginGroup("AppUpdater");
    settings_->setValue("States", appUpdaterStates_);
    settings_->endGroup();

    settings_->sync();

    qDebug() << "Config saved to:" << settings_->fileName();
    qDebug() << "IndexId set to:" << curIndexId_;
}

void Config::setDismissedReleaseTag(const QString &tag)
{
    dismissedReleaseTag_ = tag;
    save();
}

void Config::setCurModelPath(const QString &curModelPath)
{
    curModelPath_ = curModelPath;
    save();
}

void Config::setCurYoloModelPath(const QString &curYoloModelPath){
    curYoloModelPath_ = curYoloModelPath;
    save();
}

void Config::setCurIndexId(const QString &curIndexId)
{
    curIndexId_ = curIndexId;
    save();
}

void Config::setPreferredLocale(const QString &loc)
{
    preferredLocale_ = loc;
    save();
}

void Config::setCurDetectLocaleMode(DetectLocaleMode newLocaleMode)
{
    if (curDetectLocaleMode_ == newLocaleMode)
        return;
    curDetectLocaleMode_ = newLocaleMode;
    save();
    emit detectLocaleModeChanged();
}

void Config::setJpSeriestListEtag(const QString &etag) {
    jpSeriesListEtag_ = etag;
    save();
}

void Config::setEnSeriestListEtag(const QString &etag)
{
    enSeriesListEtag_ = etag;
    save();
}

void Config::setSeriesListEtag(DetectLocaleMode m, const QString &etag)
{
    (m == DetectLocaleMode::JP ? jpSeriesListEtag_ : enSeriesListEtag_) = etag;
    save();
}

void Config::setCardListEtag(const QString &id, const QString &etag) {
    curDetectLocaleMode_ == DetectLocaleMode::JP ? (cardListEtag_[id] = etag)
                                                 : (enCardListEtag_[id] = etag);
    save();
}

void Config::clearCardListEtags()
{
    cardListEtag_.clear();
    enCardListEtag_.clear();
    save();
}

void Config::clearCardListEtags(DetectLocaleMode m)
{
    (m == DetectLocaleMode::JP ? cardListEtag_ : enCardListEtag_).clear();
    save();
}

void Config::setIndexInstallPath(const QString &p)
{
    indexInstallPath_ = p;
    save();
}

void Config::setIndexManifestUrl(const QString &u)
{
    indexManifestUrl_ = u;
    save();
}

bool Config::isValidIndexName(const QString &name)
{
    QRegularExpression illegalChars("[<>:\"/\\\\|?*\\x00-\\x1F]");
    if (illegalChars.match(name).hasMatch())
        return false;

    if (name.endsWith("."))
        return false;

    if (name == "." || name == "..")
        return false;

    return true;
}

bool Config::indexNameExists(const QString &name)
{
    return QFileInfo::exists(getIndexInstallPath() + "/" + name.trimmed());
}

void Config::toggleNameCheck()
{
    disableNameCheck_ = !disableNameCheck_;
    save();
}

int Config::getMissingPurgeInterval() const
{
    return missingPurgeInterval_;
}

void Config::setMissingPurgeInterval(int interval)
{
    missingPurgeInterval_ = interval;
    save();
}

void Config::setAutomaticUpdateCheckIntervalMinutes(int minutes)
{
    minutes = qMax(1, minutes);
    if (automaticUpdateCheckIntervalMinutes_ == minutes)
        return;
    automaticUpdateCheckIntervalMinutes_ = minutes;
    save();
}

void Config::setManualUpdateCheckIntervalMinutes(int minutes)
{
    minutes = qMax(1, minutes);
    if (manualUpdateCheckIntervalMinutes_ == minutes)
        return;
    manualUpdateCheckIntervalMinutes_ = minutes;
    save();
}

qint64 Config::hourlyCheckWaitSeconds(const QString &key) const
{
    const qint64 last = updateCheckTimes_.value(key).toLongLong();

    if (last <= 0)
        return 0;

    const qint64 now = QDateTime::currentSecsSinceEpoch();

    const qint64 elapsed = qMax(qint64{0}, now - last);

    return qMax(qint64{0}, qint64{3600} - elapsed);
}

qint64 Config::updateCheckWaitSeconds(const QString &key, bool manual) const
{
    const qint64 last = updateCheckTimes_.value(key).toLongLong();
    if (last <= 0)
        return 0;
    const qint64 interval = 60LL
                            * (manual ? manualUpdateCheckIntervalMinutes_
                                      : automaticUpdateCheckIntervalMinutes_);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return qMax<qint64>(0, interval - qMax<qint64>(0, now - last));
}

void Config::recordUpdateCheck(const QString &key)
{
    updateCheckTimes_.insert(key, QDateTime::currentSecsSinceEpoch());
    save();
}

void Config::clearUpdateCheck(const QString &key)
{
    if (updateCheckTimes_.remove(key) > 0)
        save();
}

QVariantMap Config::appUpdaterState(const QString &repository) const
{
    return appUpdaterStates_.value(repository).toMap();
}

void Config::setAppUpdaterState(const QString &repository, const QVariantMap &state)
{
    if (appUpdaterStates_.value(repository).toMap() == state)
        return;

    appUpdaterStates_.insert(repository, state);
    save();
}

void Config::setUseAcceleration(bool use)
{
    useAcceleration_ = use;
    save();
}
