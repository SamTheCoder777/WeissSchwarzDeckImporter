#pragma once

#include <QString>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QUrl>

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

    QString getBaseImgUrl(const QString &imgPath) const {
        QUrl imagePath = QUrl(imgPath);

        return baseImgUrl_.resolved(imagePath).toString();
    }

    // ----- Dataset config ------

    // seriest list
    QString getJpSeriesListUrl() const {
        return jpSerieslistUrl_.toString();
    }
    QString getSeriesListDatabasePath() const { return serieslistDatabasePath_;}

    QString getJpSeriesListEtag() const { return jpSeriesListEtag_; }
    void setJpSeriestListEtag(const QString &etag);

    // card list
    QString getCardListUrl(QString &seriesId) const {
        QUrl seriesUrl = QUrl(seriesId);

        QUrl fullUrl = cardListBaseUrlStart_;
        fullUrl = fullUrl.resolved(seriesUrl);
        fullUrl = fullUrl.resolved(cardListBaseUrlEnd_);
        return fullUrl.toString();
    }
    QString getCardListDatabasePath() const { return cardListDatabasePath_;}

    QString getCardListEtag() const { return cardListEtag_; }
    void setCardListEtag(const QString &etag);

    // ---------------------------

    bool getModelNative() const {return native_;}
    int getModelImgSize() const {return imgSize_;}

private:
    Config();
    ~Config() = default;

    const QUrl baseImgUrl_ = QUrl("https://www.encoredecks.com/images/");

    std::unique_ptr<QSettings> settings_;

    QString curModelPath_;
    QString curYoloModelPath_;
    QString curIndexId_;

    // ----- Dataset config ------

    // Series List
    const QUrl jpSerieslistUrl_ = QUrl("https://www.encoredecks.com/api/serieslist/JP/");
    QString jpSeriesListEtag_;

    const QString serieslistDatabasePath_ = QDir(QCoreApplication::applicationDirPath()).filePath("seriesList.db");

    // Card List
    const QUrl cardListBaseUrlStart_ = QUrl("https://www.encoredecks.com/api/series/");
    const QUrl cardListBaseUrlEnd_ = QUrl("cardList");
    const QString cardListDatabasePath_ = QDir(QCoreApplication::applicationDirPath()).filePath("cardList.db");
    QString cardListEtag_;
    // ---------------------------

    bool native_ = true;
    int imgSize_ = 336;
};


