#include "Config.h"

#include <QCoreApplication>
#include <QSettings>


Config& Config::instance() {
    static Config instance;
    return instance;
}

Config::Config() {
    QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    settings_ = std::make_unique<QSettings>(configPath, QSettings::IniFormat);

    load();
}

void Config::load() {
    settings_->beginGroup("Path");

    curModelPath_ = settings_->value("ModelPath").toString();
    curIndexId_ = settings_->value("IndexId").toString();

    settings_->endGroup();
}

void Config::save() {
    settings_->beginGroup("Path");

    settings_->setValue("ModelPath", curModelPath_);
    settings_->setValue("IndexId", curIndexId_);

    settings_->endGroup();

    settings_->sync();

    qDebug() << "Config saved to:" << settings_->fileName();
    qDebug() << "IndexId set to:" << curIndexId_;
}

void Config::setCurModelPath(const QString &curModelPath){
    if (curModelPath_ == curModelPath) return;
    curModelPath_ = curModelPath;
    save();
}

void Config::setCurIndexId(const QString &curIndexId){
    //if (curIndexId_ == curIndexId) return;
    curIndexId_ = curIndexId;
    save();
}
