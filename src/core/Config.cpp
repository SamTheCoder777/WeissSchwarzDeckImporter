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
    settings_->beginGroup("Path");

    curModelPath_ = settings_->value("ModelPath").toString();
    curYoloModelPath_ = settings_->value("YoloModelPath").toString();
    curIndexId_ = settings_->value("IndexId").toString();

    settings_->endGroup();


    settings_->beginGroup("Dataset");

    cardListEtag_ = settings_->value("CardListEtag").value<QMap<QString, QString>>();
    jpSeriesListEtag_ = settings_->value("jpSeriesListEtag").toString();

    settings_->endGroup();

    settings_->beginGroup("Index");

    disableNameCheck_ = settings_->value("DisableNameCheck").toBool();

    settings_->endGroup();

    settings_->beginGroup("MissingCards");

    missingPurgeInterval_
        = settings_->value("MissingPurgeInterval", (int) MissingPurgeInterval::Never).toInt();

    settings_->endGroup();
}

void Config::save() {
    settings_->beginGroup("Path");

    settings_->setValue("ModelPath", curModelPath_);
    settings_->setValue("YoloModelPath", curYoloModelPath_);
    settings_->setValue("IndexId", curIndexId_);

    settings_->endGroup();

    settings_->beginGroup("Dataset");

    settings_->setValue("CardListEtag", QVariant::fromValue(cardListEtag_));
    settings_->setValue("jpSeriesListEtag", jpSeriesListEtag_);

    settings_->endGroup();

    settings_->beginGroup("Index");

    settings_->setValue("DisableNameCheck", disableNameCheck_);

    settings_->endGroup();

    settings_->beginGroup("MissingCards");

    settings_->setValue("MissingPurgeInterval", missingPurgeInterval_);

    settings_->endGroup();

    settings_->sync();

    qDebug() << "Config saved to:" << settings_->fileName();
    qDebug() << "IndexId set to:" << curIndexId_;
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

void Config::setJpSeriestListEtag(const QString &etag) {
    jpSeriesListEtag_ = etag;
    save();
}

void Config::setCardListEtag(const QString &id, const QString &etag) {
    cardListEtag_[id] = etag;
    save();
}

void Config::clearCardListEtags()
{
    cardListEtag_.clear();
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
