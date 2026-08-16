#pragma once

#include <QString>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QUrl>
#include <QStandardPaths>

using StringStringMap = QMap<QString, QString>;
Q_DECLARE_METATYPE(StringStringMap)

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

    QString getImgUrl(const QString &imgPath) const {
        QUrl imagePath = QUrl(imgPath);

        return baseImgUrl_.resolved(imagePath).toString();
    }

    QString getPreferredLocale() const { return preferredLocale_; }
    void    setPreferredLocale(const QString& loc);

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
        QUrl url(cardListBaseUrlStart_);
        QString path = url.path();
        if (!path.endsWith('/')) path += '/';
        path += seriesId;

        if (!cardListBaseUrlEnd_.isEmpty()) {
            if (!path.endsWith('/')) path += '/';
            path += cardListBaseUrlEnd_.path();
        }

        url.setPath(path);
        return url.toString();
    }
    QString getCardListDatabasePath() const { return cardListDatabasePath_;}

    QString getCardListEtag(QString &id) const { return cardListEtag_[id]; }
    void setCardListEtag(const QString &id, const QString &etag);
    void clearCardListEtags();

    // ---------------------------

    bool getModelNative() const {return native_;}
    int getModelImgSize() const {return imgSize_;}

    // --- index settings ---
    QString getIndexInstallPath() const {
        return indexInstallPath_.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                .filePath("indexes")
        : indexInstallPath_;
    }
    void setIndexInstallPath(const QString& p);
    QString getIndexManifestUrl() const {
        return indexManifestUrl_.isEmpty() ? kDefaultManifestUrl : indexManifestUrl_;
    }
    void setIndexManifestUrl(const QString& u);

private:
    Config();
    ~Config() = default;

    const QUrl baseImgUrl_ = QUrl("https://www.encoredecks.com/images/");

    std::unique_ptr<QSettings> settings_;

    QString curModelPath_;
    QString curYoloModelPath_;
    QString curIndexId_;

    QString preferredLocale_ = "EN";

    // ----- Dataset config ------

    // Series List
    const QUrl jpSerieslistUrl_ = QUrl("https://www.encoredecks.com/api/serieslist/JP/");
    QString jpSeriesListEtag_;

    const QString serieslistDatabasePath_ = QDir(QCoreApplication::applicationDirPath()).filePath("seriesList.db");

    // Card List
    const QUrl cardListBaseUrlStart_ = QUrl("https://www.encoredecks.com/api/series");
    const QUrl cardListBaseUrlEnd_ = QUrl("cards");
    const QString cardListDatabasePath_ = QDir(QCoreApplication::applicationDirPath()).filePath("cardList.db");
    QMap<QString, QString> cardListEtag_;
    // ---------------------------

    bool native_ = true;
    int imgSize_ = 336;

    // --- index settings ---
    QString kDefaultManifestUrl = "https://huggingface.co/datasets/SamTheCoder777/ws-index/raw/main/manifest.json";
    QString indexInstallPath_;
    QString indexManifestUrl_;

};


