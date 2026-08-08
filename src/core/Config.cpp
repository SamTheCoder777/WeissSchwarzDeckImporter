#include "Config.h"

#include <QCoreApplication>
#include <QSettings>


Config& Config::instance() {
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


    settings_->sync();

    qDebug() << "Config saved to:" << settings_->fileName();
    qDebug() << "IndexId set to:" << curIndexId_;
}

void Config::setCurModelPath(const QString &curModelPath){
    //if (curModelPath_ == curModelPath) return;
    curModelPath_ = curModelPath;
    save();
}

void Config::setCurYoloModelPath(const QString &curYoloModelPath){
    curYoloModelPath_ = curYoloModelPath;
    save();
}

void Config::setCurIndexId(const QString &curIndexId){
    //if (curIndexId_ == curIndexId) return;
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

