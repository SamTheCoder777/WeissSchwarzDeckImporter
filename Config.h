#pragma once

#include <QString>
#include <QSettings>

class Config : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString curIndexId READ getCurIndexId CONSTANT)
    Q_PROPERTY(QString curYoloModelPath READ getCurYoloModelPath CONSTANT)
    Q_PROPERTY(QString curModelPath READ getCurModelPath CONSTANT)
public:
    static Config& instance();

    void load();
    void save();

    QString getCurModelPath() const {return curModelPath_;}
    void setCurModelPath(const QString &curModelPath);

    QString getCurYoloModelPath() const {return curYoloModelPath_;}
    void setCurYoloModelPath(const QString &curYoloModelPath_);

    QString getCurIndexId() const {return curIndexId_;}
    void setCurIndexId(const QString &curIndexId);

private:
    Config();
    ~Config() = default;

    std::unique_ptr<QSettings> settings_;

    QString curModelPath_;
    QString curYoloModelPath_;
    QString curIndexId_;
};


