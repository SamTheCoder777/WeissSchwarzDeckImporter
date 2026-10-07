#pragma once

#include "../cardsIndex/SeriesRepository.h"
#include "../database/DatabaseUtil.h"
#include "../index/IndexCatalog.h"
#include "../services/ModelService.h"

#include <QCheckBox>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

class SettingsPage: public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(ModelService *models,
                          SeriesRepository *seriesRepository,
                          IndexCatalog *indexCatalog,
                          DatabaseUtil *dbUtil,
                          QWidget *parent = nullptr);

    bool appUpdateAvailable() const { return appUpdateAvailable_; }
    void updateModelPaths();

signals:
    void appUpdateStateChanged(bool available);

private:
    void buildUi();

    SeriesRepository* seriesRepository_;
    ModelService* models_;
    IndexCatalog* indexCatalog_;
    DatabaseUtil *dbUtil_;

    // settings app update notif
    void checkForAppUpdate();
    void setAppUpdate(bool available, const QString &tag = {}, const QDateTime &published = {});

    QFrame *updateBanner_ = nullptr;
    QLabel *updateTitle_ = nullptr;
    QLabel *updateSubtitle_ = nullptr;
    QString updateUrl_;
    QString latestTag_;
    bool appUpdateAvailable_ = false;
    QNetworkAccessManager updateNam_;

    // settings widgets
    QLineEdit* onnxEdit_;
    QLineEdit* yoloEdit_;
    QSpinBox*  imgSizeSpin_;
    QCheckBox* nativeCheck_;
    QLabel*    modelStatus_;

    // settings dataset
    QLabel *lblDatasetStatus_;
    QProgressBar *pbDataset_;
    QLineEdit *manifestUrlEdit_;
    QLineEdit *indexPathEdit_;

    // missing cards cache
    QComboBox *missingIntervalCombo_ = nullptr;

    using Region = Config::DetectLocaleMode;

    struct DatasetRow
    {
        QLabel *name = nullptr;
        QLabel *state = nullptr;
        QPushButton *downloadBtn = nullptr;
    };
    DatasetRow datasetRows_[2]; // [JP, EN]

    static int rowIndex(Region r) { return r == Region::JP ? 0 : 1; }
    static QString regionTitle(Region r) { return r == Region::JP ? "Japanese" : "English"; }
    static QString regionCode(Region r) { return r == Region::JP ? "JP" : "EN"; }

    QWidget *buildDatasetRow(Region region, QWidget *parent);
    void setDatasetRowState(Region region, SeriesRepository::UpdateStatus status);
    void setDatasetRowChecking(Region region);
    void updateActiveRegionBadges();
    bool confirmDestructive(const QString &title, const QString &text);
};