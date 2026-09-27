#include "SettingsPage.h"
#include "../core/Config.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPropertyAnimation>
#include <QUrl>
#include <QVariantAnimation>
#include <QVersionNumber>

SettingsPage::SettingsPage(ModelService *models,
                           SeriesRepository *seriesRepository,
                           IndexCatalog *indexCatalog,
                           DatabaseUtil *dbUtil,
                           QWidget *parent)
    : QWidget(parent)
    , models_(models)
    , seriesRepository_(seriesRepository)
    , indexCatalog_(indexCatalog)
    , dbUtil_(dbUtil)
{
    buildUi();
}

static QVersionNumber versionFromTag(QString tag)
{
    tag = tag.trimmed();
    if (tag.startsWith('v', Qt::CaseInsensitive))
        tag.remove(0, 1);
    return QVersionNumber::fromString(tag);
}

void SettingsPage::buildUi()
{
    setObjectName("settingsPage");

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // The page itself only holds a scroll area; all settings go inside it.
    auto *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("settingsScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet("QScrollArea#settingsScroll { background: transparent; }"
                          "QScrollArea#settingsScroll > QWidget > QWidget#settingsContent"
                          " { background: transparent; }");

    auto *content = new QWidget(scroll);
    content->setObjectName("settingsContent");
    content->setMinimumWidth(620);
    scroll->setWidget(content);
    pageLayout->addWidget(scroll);

    auto *outer = new QVBoxLayout(content);
    outer->setContentsMargins(28, 28, 28, 28);
    outer->setSpacing(20);

    // page title
    auto* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(3);

    auto* title = new QLabel("Settings", this);
    title->setObjectName("pageTitle");
    //QFont titleFont = title->font(); titleFont.setPixelSize(21); titleFont.setBold(true); title->setFont(titleFont);

    auto* subtitle = new QLabel("Configure models and dataset maintenance", this);
    subtitle->setObjectName("pageSubtitle");

    titleCol->addWidget(title);
    titleCol->addWidget(subtitle);

    headerRow->addLayout(titleCol);
    headerRow->addStretch(1);
    outer->addLayout(headerRow);

    // app update banner
    updateBanner_ = new QFrame(this);
    updateBanner_->setObjectName("updateBanner");
    updateBanner_->setStyleSheet(
        "QFrame#updateBanner { background: rgba(59,130,246,0.12);"
        " border: 1px solid rgba(59,130,246,0.45); border-radius: 10px; }"
        "QFrame#updateBanner QLabel { background: transparent; border: none; }");
    updateBanner_->setVisible(false);

    auto *bannerRow = new QHBoxLayout(updateBanner_);
    bannerRow->setContentsMargins(16, 12, 12, 12);
    bannerRow->setSpacing(12);

    auto *bannerDot = new QLabel(updateBanner_);
    bannerDot->setFixedSize(10, 10);
    bannerDot->setStyleSheet("background:#ef4444; border-radius:5px;");

    auto *bannerText = new QVBoxLayout;
    bannerText->setSpacing(2);
    updateTitle_ = new QLabel(updateBanner_);
    updateTitle_->setStyleSheet("font-weight:600; color:#3b82f6;");
    updateSubtitle_ = new QLabel(updateBanner_);
    updateSubtitle_->setStyleSheet("font-size:12px; color:#9aa0a6;");
    bannerText->addWidget(updateTitle_);
    bannerText->addWidget(updateSubtitle_);

    auto *btnViewRelease = new QPushButton("View release", updateBanner_);
    btnViewRelease->setObjectName("actionPrimary");
    btnViewRelease->setCursor(Qt::PointingHandCursor);
    btnViewRelease->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    btnViewRelease->setMinimumHeight(32);

    auto *btnDismiss = new QPushButton("Dismiss", updateBanner_);
    btnDismiss->setObjectName("actionGhost");
    btnDismiss->setCursor(Qt::PointingHandCursor);
    btnDismiss->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    btnDismiss->setMinimumHeight(32);

    bannerRow->addWidget(bannerDot, 0, Qt::AlignVCenter);
    bannerRow->addLayout(bannerText, 1);
    bannerRow->addWidget(btnViewRelease, 0, Qt::AlignVCenter);
    bannerRow->addWidget(btnDismiss, 0, Qt::AlignVCenter);
    outer->addWidget(updateBanner_);

    connect(btnViewRelease, &QPushButton::clicked, this, [this] {
        if (!updateUrl_.isEmpty())
            QDesktopServices::openUrl(QUrl(updateUrl_));
    });
    connect(btnDismiss, &QPushButton::clicked, this, [this] {
        Config::instance().setDismissedReleaseTag(latestTag_);
        setAppUpdate(false);
    });

    // Check startup and every 6 hours
    QTimer::singleShot(3000, this, &SettingsPage::checkForAppUpdate);
    auto *updateTimer = new QTimer(this);
    updateTimer->setInterval(6 * 60 * 60 * 1000);
    connect(updateTimer, &QTimer::timeout, this, &SettingsPage::checkForAppUpdate);
    updateTimer->start();

    // Detect Language Setting
    auto *langGroup = new QFrame(this);
    langGroup->setObjectName("sectionCard");

    auto *langOuter = new QVBoxLayout(langGroup);
    langOuter->setContentsMargins(20, 20, 20, 20);
    langOuter->setSpacing(16);

    auto *langToggleFrame = new QFrame(this);
    langToggleFrame->setObjectName("langToggle");
    langToggleFrame->setFixedHeight(36);
    langToggleFrame->setFixedWidth(140);

    auto *langToggleLayout = new QHBoxLayout(langToggleFrame);
    langToggleLayout->setContentsMargins(3, 3, 3, 3);
    langToggleLayout->setSpacing(0);

    auto *highlight = new QFrame(langToggleFrame);
    highlight->setObjectName("langHighlight");
    highlight->setGeometry(3, 3, 67, 30);
    highlight->lower();

    auto *jpButton = new QPushButton("JP", langToggleFrame);
    auto *enButton = new QPushButton("EN", langToggleFrame);

    jpButton->setCheckable(true);
    enButton->setCheckable(true);

    auto *langButtonGroup = new QButtonGroup(langToggleFrame);
    langButtonGroup->setExclusive(true);
    langButtonGroup->addButton(jpButton);
    langButtonGroup->addButton(enButton);

    bool isEnglish = Config::instance().getCurDetectLocaleMode() == Config::DetectLocaleMode::EN;
    jpButton->setChecked(!isEnglish);
    enButton->setChecked(isEnglish);

    QRect jpGeometry(3, 3, 67, 30);
    QRect enGeometry(70, 3, 67, 30);
    highlight->setGeometry(isEnglish ? enGeometry : jpGeometry);

    static const QColor jpColor(0x5b, 0x7f, 0xb5);
    static const QColor enColor(0x22, 0xc5, 0x5e);
    highlight->setStyleSheet(
        QString("QFrame#langHighlight { background-color: %1; border-radius: 8px; }")
            .arg((isEnglish ? enColor : jpColor).name()));

    langToggleLayout->addWidget(jpButton);
    langToggleLayout->addWidget(enButton);

    langToggleFrame->setStyleSheet("QFrame#langToggle {"
                                   "    background-color: #e2e8f0;"
                                   "    border-radius: 10px;"
                                   "}"
                                   "QPushButton {"
                                   "    border: none;"
                                   "    border-radius: 8px;"
                                   "    padding: 4px 16px;"
                                   "    font-size: 13px;"
                                   "    font-weight: 600;"
                                   "    color: #3b82f6;"
                                   "    background-color: transparent;"
                                   "}"
                                   "QPushButton:checked {"
                                   "    color: #ffffff;"
                                   "}"
                                   "QPushButton:hover:!checked {"
                                   "    color: #334155;"
                                   "}");

    highlight->setStyleSheet(
        "QFrame#langHighlight { background-color: #3b82f6; border-radius: 8px; }");

    auto *slideAnim = new QPropertyAnimation(highlight, "geometry", langToggleFrame);
    slideAnim->setDuration(220);
    slideAnim->setEasingCurve(QEasingCurve::OutCubic);

    QObject::connect(enButton, &QPushButton::toggled, [=](bool isChecked) {
        Config::instance().setCurDetectLocaleMode(isChecked ? Config::DetectLocaleMode::EN
                                                            : Config::DetectLocaleMode::JP);

        slideAnim->stop();
        slideAnim->setStartValue(highlight->geometry());
        slideAnim->setEndValue(isChecked ? enGeometry : jpGeometry);
        slideAnim->start();

        auto *colorAnim = new QVariantAnimation(langToggleFrame);
        colorAnim->setDuration(220);
        colorAnim->setStartValue(isChecked ? jpColor : enColor);
        colorAnim->setEndValue(isChecked ? enColor : jpColor);
        QObject::connect(
            colorAnim,
            &QVariantAnimation::valueChanged,
            highlight,
            [highlight](const QVariant &value) {
                QColor c = value.value<QColor>();
                highlight->setStyleSheet(
                    QString("QFrame#langHighlight { background-color: %1; border-radius: 8px; }")
                        .arg(c.name()));
            });
        colorAnim->start(QAbstractAnimation::DeleteWhenStopped);
    });

    auto *langTitle = new QLabel("Set Detect Language", langGroup);
    langOuter->addWidget(langTitle);

    auto *langRow = new QHBoxLayout;
    langRow->addWidget(langToggleFrame, 7);
    langRow->addStretch(3);

    langOuter->addLayout(langRow);
    outer->addWidget(langGroup);

    // model paths section
    auto *modelGroup = new QFrame(this);
    modelGroup->setObjectName("sectionCard");

    auto* modelOuter = new QVBoxLayout(modelGroup);
    modelOuter->setContentsMargins(20, 20, 20, 20);
    modelOuter->setSpacing(16);

    auto* modelHeading = new QLabel("Model paths", modelGroup);
    modelHeading->setObjectName("sectionHeading");
    modelOuter->addWidget(modelHeading);

    QFormLayout* modelForm = new QFormLayout;
    modelForm->setSpacing(12);
    modelForm->setContentsMargins(0, 0, 0, 0);
    modelForm->setLabelAlignment(Qt::AlignLeft);
    modelForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    modelOuter->addLayout(modelForm);

    auto browseRow = [this, modelForm, modelGroup](QLineEdit*& edit, const QString& label, bool dir) {
        edit = new QLineEdit(modelGroup);
        edit->setMinimumHeight(36);

        auto* btn = new QPushButton("Browse", modelGroup);
        btn->setObjectName("actionGhost");
        btn->setCursor(Qt::PointingHandCursor);
        btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        btn->setMinimumHeight(36);

        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(edit);
        row->addWidget(btn);
        modelForm->addRow(label, row);

        connect(btn, &QPushButton::clicked, this, [this, edit, dir] {
            QString p = dir ? QFileDialog::getExistingDirectory(this, "Select folder")
                            : QFileDialog::getOpenFileName(this, "Select file");

            if (p.isEmpty())
                return;

            QDir appDir(QCoreApplication::applicationDirPath());
            QString canonicalPath = QDir(p).canonicalPath();

            if (canonicalPath.startsWith(appDir.canonicalPath())) {
                edit->setText(appDir.relativeFilePath(canonicalPath));
            } else {
                edit->setText(p);
            }
        });
    };
    browseRow(onnxEdit_, "Card Identifier", false);
    browseRow(yoloEdit_, "Card Detector", false);

    bool modelPathLoaded = !Config::instance().getCurModelPath().isNull()
                        && !Config::instance().getCurModelPath().isEmpty();
    onnxEdit_->setText(modelPathLoaded ? Config::instance().getCurModelPath() : "");
    onnxEdit_->setReadOnly(true);
    bool yoloModelPathLoaded = !Config::instance().getCurYoloModelPath().isNull()
                            && !Config::instance().getCurYoloModelPath().isEmpty();
    yoloEdit_->setText(yoloModelPathLoaded ? Config::instance().getCurYoloModelPath() : "");
    yoloEdit_->setReadOnly(true);

    if (modelPathLoaded && yoloModelPathLoaded) {
        QTimer::singleShot(0, this, [this]() { models_->load(true); });
    }

    auto* loadBtn = new QPushButton("Load models", modelGroup);
    loadBtn->setObjectName("actionPrimary");
    loadBtn->setCursor(Qt::PointingHandCursor);
    loadBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* loadBtnRow = new QHBoxLayout();
    loadBtnRow->setContentsMargins(0, 4, 0, 0);
    loadBtnRow->addWidget(loadBtn);
    loadBtnRow->addStretch(1);
    modelOuter->addLayout(loadBtnRow);

    modelStatus_ = new QLabel("No model loaded.", modelGroup);
    modelStatus_->setObjectName("statusLabel");
    modelStatus_->setWordWrap(true);
    modelOuter->addWidget(modelStatus_);

    QHBoxLayout *HDlLinks = new QHBoxLayout();

    QLabel *DownloadHint = new QLabel("Download:");
    DownloadHint->setStyleSheet("font-weight: 500;");

    auto makeLinkLabel = [](const QString &url, const QString &text) {
        QLabel *label = new QLabel();
        label->setTextFormat(Qt::RichText);
        label->setText(QString("<a href='%1' style='color:#3daee9; text-decoration:none;'>%2</a>")
                           .arg(url, text));
        label->setOpenExternalLinks(true);
        label->setStyleSheet("QLabel { padding: 0px; } "
                             "QLabel:hover { text-decoration: underline; }");
        return label;
    };

    QLabel *CardIdentifierDlHint = makeLinkLabel(Config::instance().CardIdentifierDl_,
                                                 "Card Identifier");
    QLabel *CardDetectorDlHint = makeLinkLabel(Config::instance().CardDetectorDl_, "Card Detector");

    QFrame *sep = new QFrame();
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    sep->setFixedHeight(12);

    HDlLinks->addWidget(DownloadHint);
    HDlLinks->addWidget(CardIdentifierDlHint);
    HDlLinks->addSpacing(6);
    HDlLinks->addWidget(sep);
    HDlLinks->addSpacing(6);
    HDlLinks->addWidget(CardDetectorDlHint);
    HDlLinks->addStretch(1);
    HDlLinks->setSpacing(6);
    HDlLinks->setContentsMargins(0, 0, 0, 0);
    HDlLinks->setAlignment(Qt::AlignLeft);

    modelOuter->addLayout(HDlLinks);

    outer->addWidget(modelGroup);

    connect(loadBtn, &QPushButton::clicked, this, [this]{
        if(models_->isLoading()) return;
        try{
            models_->load(onnxEdit_->text(),
                          yoloEdit_->text(),
                          Config::instance().getModelNative(),
                          Config::instance().getModelImgSize(),
                          false);
        }catch(const std::exception& e){
            QMessageBox::critical(this, "Error Loading Model", QString::fromStdString(e.what()));
        }
    });

    connect(models_, &ModelService::statusChanged, this, [this](const QString &statusText) {
        qDebug()<<statusText;
        modelStatus_->setText(statusText);
    });

    connect(models_, &ModelService::loaded, this, [this](bool ok, const QString &message) {
        if(!ok){
            QMessageBox::critical(this, "Model Service Error", message);
        }
        qDebug()<<message;
    });

    // Index settings
    auto* indexGroup = new QFrame(this);
    indexGroup->setObjectName("sectionCard");

    auto* indexOuter = new QVBoxLayout(indexGroup);
    indexOuter->setContentsMargins(20, 20, 20, 20);
    indexOuter->setSpacing(16);

    auto* indexHeading = new QLabel("Index paths", indexGroup);
    indexHeading->setObjectName("sectionHeading");
    indexOuter->addWidget(indexHeading);

    QFormLayout* indexForm = new QFormLayout;
    indexForm->setSpacing(12);
    indexForm->setContentsMargins(0, 0, 0, 0);
    indexForm->setLabelAlignment(Qt::AlignLeft);
    indexForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    indexOuter->addLayout(indexForm);

    {
        indexPathEdit_ = new QLineEdit(Config::instance().getIndexInstallPath(), indexGroup);
        indexPathEdit_->setReadOnly(true);
        indexPathEdit_->setMinimumHeight(36);

        auto* btnBrowse = new QPushButton("Browse", indexGroup);
        btnBrowse->setObjectName("actionGhost");
        btnBrowse->setCursor(Qt::PointingHandCursor);
        btnBrowse->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        btnBrowse->setMinimumHeight(36);

        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(indexPathEdit_);
        row->addWidget(btnBrowse);
        indexForm->addRow("Index folder", row);

        connect(btnBrowse, &QPushButton::clicked, this, [this]{
            QString p = QFileDialog::getExistingDirectory(this, "Select index folder",
                                                          Config::instance().getIndexInstallPath());
            if (!p.isEmpty()) {
                Config::instance().setIndexInstallPath(p);
                indexPathEdit_->setText(p);
                if (indexCatalog_) indexCatalog_->refresh();
            }
        });
    }

    {
        manifestUrlEdit_ = new QLineEdit(Config::instance().getIndexManifestUrl(), indexGroup);
        manifestUrlEdit_->setMinimumHeight(36);
        indexForm->addRow("Manifest URL", manifestUrlEdit_);

        connect(manifestUrlEdit_, &QLineEdit::editingFinished, this, [this]{
            Config::instance().setIndexManifestUrl(manifestUrlEdit_->text().trimmed());
            if (indexCatalog_) indexCatalog_->refresh();
        });
    }

    outer->addWidget(indexGroup);

    // dataset maintenance
    auto* datasetGroup = new QFrame(this);
    datasetGroup->setObjectName("sectionCard");

    auto* groupLayout = new QVBoxLayout(datasetGroup);
    groupLayout->setContentsMargins(20, 20, 20, 20);
    groupLayout->setSpacing(16);

    auto* datasetHeading = new QLabel("Dataset Maintenance", datasetGroup);
    datasetHeading->setObjectName("sectionHeading");
    groupLayout->addWidget(datasetHeading);

    auto *datasetDesc = new QLabel("Series lists are stored separately for each card language. "
                                   "Detection uses the one matching Detect Language.",
                                   datasetGroup);
    datasetDesc->setWordWrap(true);
    datasetDesc->setStyleSheet("color:#9aa0a6; font-size:12px;");
    groupLayout->addWidget(datasetDesc);

    groupLayout->addWidget(buildDatasetRow(Region::JP, datasetGroup));
    groupLayout->addWidget(buildDatasetRow(Region::EN, datasetGroup));

    pbDataset_ = new QProgressBar(datasetGroup);
    pbDataset_->setRange(0, 100);
    pbDataset_->setValue(0);
    pbDataset_->setTextVisible(true);
    pbDataset_->setFixedHeight(16);
    pbDataset_->setVisible(false);
    groupLayout->addWidget(pbDataset_);

    lblDatasetStatus_ = new QLabel("", datasetGroup);
    lblDatasetStatus_->setObjectName("statusLabel");
    lblDatasetStatus_->setWordWrap(true);
    lblDatasetStatus_->setVisible(false);
    groupLayout->addWidget(lblDatasetStatus_);

    outer->addWidget(datasetGroup);

    connect(seriesRepository_, &SeriesRepository::statusChanged, this, [this](const QString &s) {
        lblDatasetStatus_->setVisible(!s.isEmpty());
        lblDatasetStatus_->setText(s);
    });

    connect(seriesRepository_,
            &SeriesRepository::updateAvailable,
            this,
            [this](Config::DetectLocaleMode region, SeriesRepository::UpdateStatus status) {
                setDatasetRowState(region, status);
            });

    connect(seriesRepository_, &SeriesRepository::busyChanged, this, [this] {
        const bool busy = seriesRepository_->isBusy();
        for (auto &r : datasetRows_)
            r.downloadBtn->setEnabled(!busy);
        if (!busy)
            pbDataset_->setVisible(false);
    });

    connect(seriesRepository_,
            &SeriesRepository::seriesProgress,
            this,
            [this](Config::DetectLocaleMode region, qint64 got, qint64 total) {
                const QString what = regionTitle(region) + " series list";
                const double recMB = got / (1024.0 * 1024.0);
                pbDataset_->setVisible(true);
                lblDatasetStatus_->setVisible(true);
                if (total > 0) {
                    pbDataset_->setRange(0, 100);
                    const int percent = static_cast<int>((got * 100) / total);
                    pbDataset_->setValue(percent);
                    lblDatasetStatus_->setText(QString("Downloading %1: %2 MB / %3 MB (%4%)")
                                                   .arg(what)
                                                   .arg(recMB, 0, 'f', 1)
                                                   .arg(total / (1024.0 * 1024.0), 0, 'f', 1)
                                                   .arg(percent));
                } else if (got > 0) {
                    pbDataset_->setRange(0, 0);
                    lblDatasetStatus_->setText(
                        QString("Downloading %1: %2 MB…").arg(what).arg(recMB, 0, 'f', 1));
                }
            });

    connect(&Config::instance(),
            &Config::detectLocaleModeChanged,
            this,
            &SettingsPage::updateActiveRegionBadges);

    setDatasetRowChecking(Region::JP);
    setDatasetRowChecking(Region::EN);
    updateActiveRegionBadges();
    QTimer::singleShot(0, this, [this] {
        seriesRepository_->checkForUpdates(Region::JP);
        seriesRepository_->checkForUpdates(Region::EN);
    });

    // Missing card cache settings
    auto *missingGroup = new QFrame(this);
    missingGroup->setObjectName("sectionCard");

    auto *missingOuter = new QVBoxLayout(missingGroup);
    missingOuter->setContentsMargins(20, 20, 20, 20);
    missingOuter->setSpacing(16);

    auto *missingHeading = new QLabel("Missing-card cache", missingGroup);
    missingHeading->setObjectName("sectionHeading");
    missingOuter->addWidget(missingHeading);

    auto *missingDesc
        = new QLabel("Cards not found in either database are remembered so they aren't looked up "
                     "again. Choose how long to remember them before re-checking.",
                     missingGroup);
    missingDesc->setWordWrap(true);
    missingDesc->setStyleSheet("color:#9aa0a6; font-size:12px;");
    missingOuter->addWidget(missingDesc);

    auto *missingForm = new QFormLayout;
    missingForm->setSpacing(12);
    missingForm->setContentsMargins(0, 0, 0, 0);
    missingForm->setLabelAlignment(Qt::AlignLeft);
    missingForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    missingOuter->addLayout(missingForm);

    missingIntervalCombo_ = new QComboBox(missingGroup);
    missingIntervalCombo_->addItem("1 hour", (int) Config::MissingPurgeInterval::Hourly);
    missingIntervalCombo_->addItem("1 day", (int) Config::MissingPurgeInterval::Daily);
    missingIntervalCombo_->addItem("7 days", (int) Config::MissingPurgeInterval::Weekly);
    missingIntervalCombo_->addItem("1 month", (int) Config::MissingPurgeInterval::Monthly);
    missingIntervalCombo_->addItem("Never", (int) Config::MissingPurgeInterval::Never);

    {
        int cur = Config::instance().getMissingPurgeInterval();
        int idx = missingIntervalCombo_->findData(cur);
        if (idx >= 0)
            missingIntervalCombo_->setCurrentIndex(idx);
    }
    missingForm->addRow("Re-check missing cards after", missingIntervalCombo_);

    connect(missingIntervalCombo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [this](int) {
                Config::instance().setMissingPurgeInterval(
                    missingIntervalCombo_->currentData().toInt());
            });

    auto *btnPurgeMissing = new QPushButton("Clear now", missingGroup);
    btnPurgeMissing->setObjectName("actionGhost");
    missingForm->addRow("Reset cache", btnPurgeMissing);

    connect(btnPurgeMissing, &QPushButton::clicked, this, [this] { dbUtil_->purgeMissingCards(); });

    outer->addWidget(missingGroup);

    connect(dbUtil_, &DatabaseUtil::missingCardsPurged, this, [this](int n) {
        QMessageBox::information(this,
                                 "Cache cleared",
                                 QString("Cleared %1 remembered missing-card record(s). "
                                         "They'll be re-checked when next viewed.")
                                     .arg(n));
    });

    // advanced settings
    auto* advToggle = new QPushButton("▸ Advanced settings");
    advToggle->setObjectName("advToggle");
    advToggle->setCheckable(true);
    advToggle->setCursor(Qt::PointingHandCursor);
    advToggle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* advToggleRow = new QHBoxLayout();
    advToggleRow->setContentsMargins(0, 4, 0, 0);
    advToggleRow->addWidget(advToggle);
    advToggleRow->addStretch(1);
    outer->addLayout(advToggleRow);

    auto* advWidget = new QFrame;
    advWidget->setObjectName("sectionCard");

    auto* advForm = new QFormLayout(advWidget);
    advForm->setContentsMargins(20, 20, 20, 20);
    advForm->setSpacing(12);
    advForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto *maintTitle = new QLabel("Data reset", advWidget);
    maintTitle->setStyleSheet("font-weight:600;");
    advForm->addRow(maintTitle);

    auto *maintGrid = new QGridLayout;
    maintGrid->setContentsMargins(0, 0, 0, 0);
    maintGrid->setHorizontalSpacing(10);
    maintGrid->setVerticalSpacing(8);

    const QStringList headers = {"Series list", "Card data", "Official fallbacks"};
    for (int c = 0; c < headers.size(); ++c) {
        auto *h = new QLabel(headers[c], advWidget);
        h->setStyleSheet("color:#9aa0a6; font-size:12px;");
        maintGrid->addWidget(h, 0, c + 1);
    }

    auto makeDangerBtn = [advWidget](const QString &text) {
        auto *b = new QPushButton(text, advWidget);
        b->setObjectName("actionError");
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        b->setMinimumHeight(32);
        b->setMinimumWidth(110);
        return b;
    };

    int gridRow = 1;
    for (Region region : {Region::JP, Region::EN}) {
        const QString lang = regionTitle(region);

        auto *label = new QLabel(lang, advWidget);
        maintGrid->addWidget(label, gridRow, 0);

        auto *btnSeries = makeDangerBtn("Reset");
        auto *btnCards = makeDangerBtn("Reset");
        auto *btnFallback = makeDangerBtn("Purge");
        maintGrid->addWidget(btnSeries, gridRow, 1);
        maintGrid->addWidget(btnCards, gridRow, 2);
        maintGrid->addWidget(btnFallback, gridRow, 3);

        connect(btnSeries, &QPushButton::clicked, this, [this, region, lang] {
            if (confirmDestructive("Reset " + lang + " series list",
                                   "This deletes the downloaded " + lang
                                       + " series list. "
                                         "You can download it again from Dataset Maintenance."))
                seriesRepository_->resetSeries(region);
        });
        connect(btnCards, &QPushButton::clicked, this, [this, region, lang] {
            if (confirmDestructive("Reset " + lang + " card data",
                                   "This deletes all downloaded " + lang
                                       + " card lists, "
                                         "including cards fetched from the EncoreDecks."))
                seriesRepository_->resetCards(region);
        });
        connect(btnFallback, &QPushButton::clicked, this, [this, region, lang] {
            if (confirmDestructive("Purge " + lang + " fallback cards",
                                   "This removes " + lang
                                       + " cards that were fetched from the official "
                                         "site. They'll be fetched again when next viewed."))
                seriesRepository_->purgeFallbackCards(region);
        });
        ++gridRow;
    }
    maintGrid->setColumnStretch(4, 1);
    advForm->addRow(maintGrid);

    connect(seriesRepository_,
            &SeriesRepository::fallbackCardsPurged,
            this,
            [this](Config::DetectLocaleMode region, int n) {
                QMessageBox::information(
                    this,
                    "Purge complete",
                    QString("Removed %1 %2 fallback card(s). They will be re-fetched "
                            "from EncoreDecks or the official site next time they're viewed.")
                        .arg(n)
                        .arg(regionTitle(region)));
            });

    // imgSizeSpin_ = new QSpinBox;
    // imgSizeSpin_->setRange(64, 1024);
    // imgSizeSpin_->setSingleStep(16);
    // imgSizeSpin_->setValue(336);
    // imgSizeSpin_->setFixedWidth(120);
    // imgSizeSpin_->setMinimumHeight(32);
    // advForm->addRow("Image size", imgSizeSpin_);

    // nativeCheck_ = new QCheckBox("Native aspect ratio");
    // nativeCheck_->setChecked(true);
    // advForm->addRow("", nativeCheck_);

    QCheckBox *accCheck = new QCheckBox("Use Hardware Acceleration", this);
    accCheck->setChecked(Config::instance().getUseAcceleration());

    QLabel *restartHint = new QLabel("Applies on next model load", this);
    restartHint->setStyleSheet("color: #999; font-size: 11px;");
    restartHint->setVisible(false);

    bool prevState = Config::instance().getUseAcceleration();

    connect(accCheck,
            &QCheckBox::checkStateChanged,
            this,
            [restartHint, prevState](Qt::CheckState state) mutable {
                bool newState = (state == Qt::Checked);
                Config::instance().setUseAcceleration(newState);
                restartHint->setVisible(newState != prevState);
                prevState = newState;
            });

    advForm->addRow("", accCheck);
    advForm->addRow("", restartHint);

    advWidget->setVisible(false);
    outer->addWidget(advWidget);

    connect(advToggle,
            &QPushButton::toggled,
            this,
            [advToggle, advWidget, content, outer, scroll](bool on) {
                content->setUpdatesEnabled(false);
                advWidget->setVisible(on);
                outer->activate();
                content->setUpdatesEnabled(true);

                if (on)
                    QTimer::singleShot(0, scroll, [scroll, advWidget] {
                        scroll->ensureWidgetVisible(advWidget, 0, 20);
                    });

                advToggle->setText(on ? "▾ Advanced settings" : "▸ Advanced settings");
            });

    outer->addStretch(1);
}

QWidget *SettingsPage::buildDatasetRow(Region region, QWidget *parent)
{
    auto *row = new QFrame(parent);
    row->setObjectName("datasetRow");
    row->setStyleSheet("QFrame#datasetRow { border: 1px solid rgba(127,127,127,0.25);"
                       " border-radius: 8px; }");

    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(14, 10, 14, 10);
    h->setSpacing(12);

    auto *badge = new QLabel(regionCode(region), row);
    badge->setFixedSize(34, 22);
    badge->setAlignment(Qt::AlignCenter);
    badge->setStyleSheet(QString("background:%1; color:white; border-radius:6px;"
                                 " font-weight:600; font-size:11px; border:none;")
                             .arg(region == Region::JP ? "#5b7fb5" : "#22c55e"));

    auto *textCol = new QVBoxLayout;
    textCol->setSpacing(2);
    auto *name = new QLabel(row);
    name->setTextFormat(Qt::RichText);
    name->setStyleSheet("font-weight:600; border:none;");
    auto *state = new QLabel(row);
    state->setStyleSheet("font-size:12px; border:none;");
    textCol->addWidget(name);
    textCol->addWidget(state);

    auto *btn = new QPushButton("Download", row);
    btn->setObjectName("actionPrimary");
    btn->setCursor(Qt::PointingHandCursor);
    btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    btn->setMinimumHeight(34);
    btn->setMinimumWidth(130);

    h->addWidget(badge, 0, Qt::AlignVCenter);
    h->addLayout(textCol, 1);
    h->addWidget(btn, 0, Qt::AlignVCenter);

    DatasetRow &r = datasetRows_[rowIndex(region)];
    r.name = name;
    r.state = state;
    r.downloadBtn = btn;

    connect(btn, &QPushButton::clicked, this, [this, region] {
        if (seriesRepository_->isBusy())
            return;
        pbDataset_->setRange(0, 100);
        pbDataset_->setValue(0);
        pbDataset_->setVisible(true);
        seriesRepository_->refreshSeriesList(region);
    });

    return row;
}

void SettingsPage::setDatasetRowState(Region region, SeriesRepository::UpdateStatus status)
{
    DatasetRow &r = datasetRows_[rowIndex(region)];

    QString text, color, btnText, btnObject;
    QIcon icon;
    switch (status) {
    case SeriesRepository::UpdateStatus::UpdateAvailable:
        text = "Update available";
        color = "#f59e0b";
        btnText = "Update now";
        btnObject = "actionAccent";
        break;
    case SeriesRepository::UpdateStatus::UpToDate:
        text = "Up to date";
        color = "#22c55e";
        btnText = "Redownload";
        btnObject = "actionGhost";
        break;
    case SeriesRepository::UpdateStatus::Error:
        text = "Download needed";
        color = "#ef4444";
        btnText = "Download";
        btnObject = "actionError";
        icon = QIcon(":/icon/error.svg");
        break;
    }

    r.state->setText(text);
    r.state->setStyleSheet(QString("font-size:12px; border:none; color:%1;").arg(color));
    r.downloadBtn->setText(btnText);
    r.downloadBtn->setIcon(icon);
    r.downloadBtn->setObjectName(btnObject);
    r.downloadBtn->style()->unpolish(r.downloadBtn);
    r.downloadBtn->style()->polish(r.downloadBtn);
}

void SettingsPage::setDatasetRowChecking(Region region)
{
    DatasetRow &r = datasetRows_[rowIndex(region)];
    r.state->setText("Checking…");
    r.state->setStyleSheet("font-size:12px; border:none; color:#9aa0a6;");
}

void SettingsPage::updateActiveRegionBadges()
{
    const Region active = Config::instance().getCurDetectLocaleMode();
    for (Region region : {Region::JP, Region::EN}) {
        QString html = regionTitle(region) + " series list";
        if (region == active)
            html += "&nbsp;&nbsp;<span style='color:#9aa0a6; font-weight:400;'>· in use</span>";
        datasetRows_[rowIndex(region)].name->setText(html);
    }
}

bool SettingsPage::confirmDestructive(const QString &title, const QString &text)
{
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Cancel | QMessageBox::Yes, this);
    box.button(QMessageBox::Yes)->setText("Continue");
    box.setDefaultButton(QMessageBox::Cancel);
    return box.exec() == QMessageBox::Yes;
}

void SettingsPage::checkForAppUpdate()
{
    const QVersionNumber current = versionFromTag(QStringLiteral(APP_VERSION));
    if (current.isNull())
        return;

    QNetworkRequest req(QUrl("https://api.github.com/repos/" + Config::instance().releaseRepo_
                             + "/releases/latest"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setHeader(QNetworkRequest::UserAgentHeader, QByteArray("WSDeckImporter/") + APP_VERSION);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000);

    QNetworkReply *reply = updateNam_.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, current] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "[AppUpdate] check failed:" << reply->errorString();
            return;
        }

        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = o.value("tag_name").toString();
        const QString url = o.value("html_url").toString();
        const QDateTime published = QDateTime::fromString(o.value("published_at").toString(),
                                                          Qt::ISODate);
        if (tag.isEmpty() || url.isEmpty())
            return;

        latestTag_ = tag;
        updateUrl_ = url;

        const bool newer = versionFromTag(tag) > current;
        const bool dismissed = Config::instance().getDismissedReleaseTag() == tag;
        qDebug() << "[AppUpdate] current" << APP_VERSION << "latest" << tag
                 << (newer ? "(newer)" : "(up to date)");

        setAppUpdate(newer && !dismissed, tag, published);
    });
}

void SettingsPage::setAppUpdate(bool available, const QString &tag, const QDateTime &published)
{
    if (available) {
        updateTitle_->setText(QString("Version %1 is available").arg(tag));
        QString sub = QString("You have %1").arg(QStringLiteral(APP_VERSION));
        if (published.isValid())
            sub += "  ·  Released "
                   + QLocale().toString(published.toLocalTime().date(), QLocale::ShortFormat);
        updateSubtitle_->setText(sub);
    }
    updateBanner_->setVisible(available);

    if (available != appUpdateAvailable_) {
        appUpdateAvailable_ = available;
        emit appUpdateStateChanged(available);
    }
}