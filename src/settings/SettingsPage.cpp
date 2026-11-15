#include "SettingsPage.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>

#include <QDesktopServices>
#include <QLocale>
#include <QPropertyAnimation>
#include <QUrl>
#include <QVariantAnimation>

#include "../core/AppUpdateController.h"
#include "../core/Config.h"
#include "../network/NetGuard.h"

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
    appUpdater_ = new AppUpdateController(this);
    buildUi();
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

    // Version pill
    const QString ver = QStringLiteral(APP_VERSION);
    const QString releasesUrl = "https://github.com/" + Config::instance().releaseRepo_
                                + "/releases";
    auto *versionLabel = new QLabel(this);
    versionLabel->setTextFormat(Qt::RichText);
    versionLabel->setText(
        ver.isEmpty()
            ? QStringLiteral("Development build")
            : QString(
                  "<a href=\"%1\" style=\"color:#9aa0a6; text-decoration:none;\">Version %2</a>")
                  .arg(releasesUrl, ver.toHtmlEscaped()));
    versionLabel->setOpenExternalLinks(true);
    versionLabel->setToolTip(ver.isEmpty() ? QString() : "View release notes on GitHub");
    versionLabel->setCursor(ver.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    versionLabel->setStyleSheet("QLabel { color:#9aa0a6; font-size:12px; padding:4px 12px;"
                                " border:1px solid rgba(127,127,127,0.3); border-radius:11px; }"
                                "QLabel:hover { border-color: rgba(74,163,255,0.7); }");
    headerRow->addWidget(versionLabel, 0, Qt::AlignVCenter);

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

    // Primary installation action.
    auto *btnUpdate = new QPushButton("Update now", updateBanner_);
    btnUpdate->setObjectName("actionPrimary");
    btnUpdate->setCursor(Qt::PointingHandCursor);
    btnUpdate->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    btnUpdate->setMinimumHeight(32);

    // Separate browser action.
    auto *btnViewRelease = new QPushButton("View release", updateBanner_);
    btnViewRelease->setObjectName("actionGhost");
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
    bannerRow->addWidget(btnUpdate, 0, Qt::AlignVCenter);
    bannerRow->addWidget(btnViewRelease, 0, Qt::AlignVCenter);
    bannerRow->addWidget(btnDismiss, 0, Qt::AlignVCenter);

    outer->addWidget(updateBanner_);

    // Start automatic installation
    connect(btnUpdate, &QPushButton::clicked, this, [this] { appUpdater_->install(); });

    // Use the action label selected by the controller
    connect(appUpdater_,
            &AppUpdateController::actionTextChanged,
            this,
            [btnUpdate, btnDismiss](const QString &text) {
                btnUpdate->setText(text);

                // An interrupted installation must not be dismissed.
                btnDismiss->setEnabled(text != QStringLiteral("Repair interrupted update"));
            });

    // Receive release information and update the existing banner.
    connect(appUpdater_,
            &AppUpdateController::releaseDetected,
            this,
            [this](bool available,
                   const QString &tag,
                   const QString &releaseUrl,
                   const QDateTime &published) {
                latestTag_ = tag;
                updateUrl_ = releaseUrl;

                setAppUpdate(available, tag, published);

                if (available && tag == QStringLiteral("interrupted update")) {
                    updateTitle_->setText(QStringLiteral("An interrupted update needs repair"));

                    updateSubtitle_->setText(
                        QStringLiteral("Close other app instances, then choose "
                                       "Repair interrupted update."));
                }
            });

    // Open release notes/downloads in the browser.
    connect(btnViewRelease, &QPushButton::clicked, this, [this] {
        if (!updateUrl_.isEmpty())
            QDesktopServices::openUrl(QUrl(updateUrl_));
    });

    connect(btnDismiss, &QPushButton::clicked, this, [this] {
        Config::instance().setDismissedReleaseTag(latestTag_);
        setAppUpdate(false);
    });

    // Manual check button in the page header
    // auto *btnCheckUpdates = new QPushButton("Check now", this);
    // btnCheckUpdates->setObjectName("actionGhost");
    // btnCheckUpdates->setCursor(Qt::PointingHandCursor);
    // btnCheckUpdates->setMinimumHeight(32);

    // headerRow->addWidget(btnCheckUpdates, 0, Qt::AlignVCenter);

    // connect(btnCheckUpdates, &QPushButton::clicked, this, [this] { appUpdater_->check(true); });

    // Initial automatic check
    QTimer::singleShot(3000, this, &SettingsPage::checkForAppUpdate);

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

    if (modelPathLoaded && yoloModelPathLoaded && Config::instance().getLoadModelsOnStartup()) {
        QTimer::singleShot(0, this, [this] {
            if (Config::instance().getLoadModelsOnStartup() && !models_->isLoading()
                && !models_->tcgCore()) {
                models_->load(true);
            }
        });
    }

    auto *startupModelsCheck = new QCheckBox("Load recognition models automatically at startup",
                                             modelGroup);
    startupModelsCheck->setChecked(Config::instance().getLoadModelsOnStartup());
    modelOuter->addWidget(startupModelsCheck);

    connect(startupModelsCheck, &QCheckBox::toggled, this, [](bool enabled) {
        Config::instance().setLoadModelsOnStartup(enabled);
    });

    auto *startupModelsHint
        = new QLabel("When disabled, models load when you open or paste an image.", modelGroup);
    startupModelsHint->setWordWrap(true);
    startupModelsHint->setStyleSheet("color:#9aa0a6; font-size:12px;");
    modelOuter->addWidget(startupModelsHint);

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

    // Dataset selection setting
    auto *sourceRow = new QHBoxLayout();
    sourceRow->setContentsMargins(0, 0, 0, 0);
    sourceRow->setSpacing(12);
    sourceRow->addWidget(new QLabel("Card data source", datasetGroup));

    auto *sourceCombo = new QComboBox(datasetGroup);
    sourceCombo->addItem("EncoreDecks", QStringLiteral("encore"));
    sourceCombo->addItem("Local database", QStringLiteral("local"));
    sourceCombo->setMinimumWidth(180);
    sourceRow->addWidget(sourceCombo);
    sourceRow->addStretch(1);
    groupLayout->addLayout(sourceRow);

    auto syncCardSource = [this, sourceCombo] {
        const QSignalBlocker blocker(sourceCombo);
        sourceCombo->setCurrentIndex(sourceCombo->findData(dbUtil_->cardDataSource()));
    };
    syncCardSource();

    connect(dbUtil_, &DatabaseUtil::cardDataSourceChanged, sourceCombo, syncCardSource);

    connect(sourceCombo,
            QOverload<int>::of(&QComboBox::activated),
            this,
            [this, sourceCombo, syncCardSource](int index) {
                const QString requested = sourceCombo->itemData(index).toString();
                dbUtil_->setCardDataSource(requested);
                syncCardSource();
                if (dbUtil_->cardDataSource() != requested) {
                    QMessageBox::warning(this,
                                         "Card data source unavailable",
                                         "The local card database could not be opened. "
                                         "Check that it is installed and available.");
                }
            });

    auto *localHeading = new QLabel("WSDeckImporter Local database", datasetGroup);

    localHeading->setObjectName("sectionHeading");
    groupLayout->addWidget(localHeading);

    auto *localRow = new QHBoxLayout;
    localRow->setContentsMargins(0, 0, 0, 0);
    localRow->setSpacing(12);

    localDatabaseStatus_ = new QLabel(datasetGroup);
    localDatabaseStatus_->setWordWrap(true);
    localDatabaseStatus_->setTextFormat(Qt::PlainText);
    localDatabaseStatus_->setStyleSheet("color:#9aa0a6; font-size:12px;");

    const QFileInfo localFile(QCoreApplication::applicationDirPath() + "/weiss-db.sqlite");

    if (Config::instance().getLocalDatabaseUpdatePending()) {
        localDatabaseStatus_->setText("Update available. Close and reopen the app to install.");
    } else if (!localFile.isFile() || localFile.size() == 0) {
        localDatabaseStatus_->setText("Not installed. Check for updates to prepare installation.");
    } else {
        localDatabaseStatus_->setText("Installed. Automatic checks run at startup every 3 hours.");
    }

    localDatabaseCheckButton_ = new QPushButton("Check for updates", datasetGroup);

    localDatabaseCheckButton_->setObjectName("actionPrimary");
    localDatabaseCheckButton_->setCursor(Qt::PointingHandCursor);
    localDatabaseCheckButton_->setMinimumHeight(34);

    localDatabaseCheckButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    localRow->addWidget(localDatabaseStatus_, 1);
    localRow->addWidget(localDatabaseCheckButton_, 0, Qt::AlignVCenter);

    groupLayout->addLayout(localRow);

    connect(localDatabaseCheckButton_,
            &QPushButton::clicked,
            this,
            &SettingsPage::checkLocalDatabaseUpdate);

    auto *localCheckTimer = new QTimer(this);
    localCheckTimer->setInterval(1000);

    connect(localCheckTimer, &QTimer::timeout, this, &SettingsPage::updateLocalDatabaseCheckButton);

    localCheckTimer->start();
    updateLocalDatabaseCheckButton();

    auto *encoreToggle = new QPushButton("▸ EncoreDecks database maintenance", datasetGroup);

    encoreToggle->setCheckable(true);
    encoreToggle->setChecked(false);
    encoreToggle->setObjectName("actionGhost");
    encoreToggle->setCursor(Qt::PointingHandCursor);

    groupLayout->addWidget(encoreToggle, 0, Qt::AlignLeft);

    auto *encoreContent = new QWidget(datasetGroup);

    auto *encoreLayout = new QVBoxLayout(encoreContent);
    encoreLayout->setContentsMargins(0, 0, 0, 0);
    encoreLayout->setSpacing(12);

    encoreContent->setVisible(false);
    groupLayout->addWidget(encoreContent);

    connect(encoreToggle, &QPushButton::toggled, this, [encoreToggle, encoreContent](bool expanded) {
        encoreContent->setVisible(expanded);

        encoreToggle->setText(expanded ? "▾ EncoreDecks database maintenance"
                                       : "▸ EncoreDecks database maintenance");
    });

    auto *datasetDesc = new QLabel("Series lists are stored separately for each card language. "
                                   "Detection uses the one matching Detect Language.",
                                   datasetGroup);
    datasetDesc->setWordWrap(true);
    datasetDesc->setStyleSheet("color:#9aa0a6; font-size:12px;");
    encoreLayout->addWidget(datasetDesc);

    encoreLayout->addWidget(buildDatasetRow(Region::JP, encoreContent));
    encoreLayout->addWidget(buildDatasetRow(Region::EN, encoreContent));

    pbDataset_ = new QProgressBar(datasetGroup);
    pbDataset_->setRange(0, 100);
    pbDataset_->setValue(0);
    pbDataset_->setTextVisible(true);
    pbDataset_->setFixedHeight(16);
    pbDataset_->setVisible(false);
    encoreLayout->addWidget(pbDataset_);

    lblDatasetStatus_ = new QLabel("", datasetGroup);
    lblDatasetStatus_->setObjectName("statusLabel");
    lblDatasetStatus_->setWordWrap(true);
    lblDatasetStatus_->setVisible(false);
    encoreLayout->addWidget(lblDatasetStatus_);

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

    connect(btnPurgeMissing, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::warning(
            this,
            "Clear card cache?",
            "Use this only when card images or details are broken and restarting "
            "the app has not fixed the problem.\n\n"
            "This clears cached images and remembered missing-card records. "
            "Images will be fetched again when viewed.\n\n"
            "Clear the cache now?",
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);

        if (answer != QMessageBox::Yes)
            return;
        dbUtil_->purgeMissingCards();
    });

    outer->addWidget(missingGroup);

    connect(dbUtil_, &DatabaseUtil::missingCardsPurged, this, [this](int n) {
        QMessageBox::information(
            this,
            "Cache cleared",
            QString("Cleared remembered missing-card record(s) and locally cached images. "
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

    const QStringList headers = {"Series list", "Card data"};
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
        maintGrid->addWidget(btnSeries, gridRow, 1);
        maintGrid->addWidget(btnCards, gridRow, 2);

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
        ++gridRow;
    }
    maintGrid->setColumnStretch(4, 1);
    advForm->addRow(maintGrid);

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

        seriesRepository_->refreshSeriesList(region, true);
        pbDataset_->setVisible(seriesRepository_->isBusy());
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
        text = "Update status not verified";
        color = "#9aa0a6";
        btnText = "Fetch series list";
        btnObject = "actionGhost";
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
    appUpdater_->check(false);
}

void SettingsPage::setAppUpdate(bool available, const QString &tag, const QDateTime &published)
{
    if (available) {
        updateTitle_->setText(QString("Version %1 is available").arg(tag));

        QString subtitle = QString("You have %1").arg(QStringLiteral(APP_VERSION));

        if (published.isValid()) {
            subtitle += QStringLiteral("  ·  Released ");

            subtitle += QLocale().toString(published.toLocalTime().date(), QLocale::ShortFormat);
        }

        updateSubtitle_->setText(subtitle);
    }

    updateBanner_->setVisible(available);

    if (available != appUpdateAvailable_) {
        appUpdateAvailable_ = available;
        emit appUpdateStateChanged(available);
    }
}

void SettingsPage::updateModelPaths()
{
    bool modelPathLoaded = !Config::instance().getCurModelPath().isNull()
                           && !Config::instance().getCurModelPath().isEmpty();
    onnxEdit_->setText(modelPathLoaded ? Config::instance().getCurModelPath() : "");
    onnxEdit_->setReadOnly(true);
    bool yoloModelPathLoaded = !Config::instance().getCurYoloModelPath().isNull()
                               && !Config::instance().getCurYoloModelPath().isEmpty();
    yoloEdit_->setText(yoloModelPathLoaded ? Config::instance().getCurYoloModelPath() : "");
    yoloEdit_->setReadOnly(true);
}

void SettingsPage::updateLocalDatabaseCheckButton()
{
    if (!localDatabaseCheckButton_)
        return;

    if (localDatabaseCheckBusy_) {
        localDatabaseCheckButton_->setEnabled(false);
        localDatabaseCheckButton_->setText("Checking…");
        return;
    }

    const auto &config = Config::instance();
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    // Both startup and manual checks count toward the 15-minute
    // minimum gap before another manual request.
    const auto remainingFrom = [now](qint64 timestamp) -> qint64 {
        if (timestamp <= 0 || timestamp > now)
            return 0;

        return std::max<qint64>(0, 15 * 60 - (now - timestamp));
    };

    const qint64 remaining = std::max(remainingFrom(config.getLocalDatabaseLastCheckUtc()),
                                      remainingFrom(config.getLocalDatabaseLastManualCheckUtc()));

    localDatabaseCheckButton_->setEnabled(remaining == 0);

    if (remaining > 0) {
        localDatabaseCheckButton_->setText(QString("Check in %1:%2")
                                               .arg(remaining / 60)
                                               .arg(remaining % 60, 2, 10, QLatin1Char('0')));
    } else {
        localDatabaseCheckButton_->setText("Check for updates");
    }
}

void SettingsPage::checkLocalDatabaseUpdate()
{
    if (localDatabaseCheckBusy_)
        return;

    updateLocalDatabaseCheckButton();

    // Enforce the cooldown here too, not only through the UI.
    if (!localDatabaseCheckButton_ || !localDatabaseCheckButton_->isEnabled()) {
        return;
    }

    auto &config = Config::instance();

    const QUrl manifestUrl(config.localDatabaseVersionUrl_);

    if (!manifestUrl.isValid() || manifestUrl.scheme() != QStringLiteral("https")
        || manifestUrl.host().isEmpty() || !manifestUrl.userInfo().isEmpty()
        || manifestUrl.hasFragment()) {
        localDatabaseStatus_->setText("Set a valid HTTPS database version URL in Config.h.");
        return;
    }

    // Save before requesting so repeated failures/restarts
    // cannot bypass the manual cooldown.
    config.setLocalDatabaseLastManualCheckUtc(QDateTime::currentSecsSinceEpoch());

    localDatabaseCheckBusy_ = true;
    localDatabaseStatus_->setText("Checking for database updates…");
    updateLocalDatabaseCheckButton();

    NetRequest request;
    request.url = manifestUrl;
    request.key = QStringLiteral("local-database-version:") + manifestUrl.toString();

    request.useCache = false;
    request.timeoutMs = 15000;
    request.retries = 0;

    NetGuard::instance().get(request, this, [this](const NetResult &result) {
        localDatabaseCheckBusy_ = false;
        updateLocalDatabaseCheckButton();

        if (!result.ok()) {
            localDatabaseStatus_->setText(QString("Could not check: %1").arg(result.message)
                                          + (Config::instance().getLocalDatabaseUpdatePending()
                                                 ? "\nA previously found update is still pending."
                                                 : ""));
            return;
        }

        if (result.body.size() > 64 * 1024) {
            localDatabaseStatus_->setText("The database version JSON is too large.");
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(result.body, &parseError);

        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            localDatabaseStatus_->setText("The database version JSON is invalid.");
            return;
        }

        const QString remoteHash = document.object().value("hash").toString().trimmed().toLower();

        static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-f]{64}$"));

        if (!hashPattern.match(remoteHash).hasMatch()) {
            localDatabaseStatus_->setText("version.json must contain a 64-character SHA-256 hash.");
            return;
        }

        auto &config = Config::instance();

        const QFileInfo file(QCoreApplication::applicationDirPath() + "/weiss-db.sqlite");

        const bool installed = file.isFile() && file.size() > 0;

        const bool updateAvailable = !installed
                                     || remoteHash
                                            != config.getLocalDatabaseHash().trimmed().toLower();

        config.setLocalDatabaseUpdatePending(updateAvailable);
        config.setLocalDatabaseLastCheckUtc(QDateTime::currentSecsSinceEpoch());

        if (updateAvailable) {
            localDatabaseStatus_->setText(installed
                                              ? "Update available. Close and reopen the app "
                                                "to download and install it."
                                              : "Database available. Close and reopen the app "
                                                "to download and install it.");
        } else {
            localDatabaseStatus_->setText("Local database is up to date.");
        }

        updateLocalDatabaseCheckButton();
    });
}