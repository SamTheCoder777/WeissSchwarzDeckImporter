#include "SettingsPage.h"
#include "../core/Config.h"

#include <QComboBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QTimer>

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

void SettingsPage::buildUi()
{
    setObjectName("settingsPage");

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* outer = new QVBoxLayout(this);
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

    // model paths section
    auto* modelGroup = new QFrame(this);
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
            if (!p.isEmpty()) edit->setText(p);
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

    btnSeriesDownload_ = new QPushButton("Download SerliesList", datasetGroup);
    btnSeriesDownload_->setObjectName("actionPrimary");
    btnSeriesDownload_->setCursor(Qt::PointingHandCursor);
    btnSeriesDownload_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    btnSeriesDownload_->setMinimumHeight(36);

    auto* datasetBtnRow = new QHBoxLayout();
    datasetBtnRow->setContentsMargins(0, 0, 0, 0);
    datasetBtnRow->addWidget(btnSeriesDownload_);
    datasetBtnRow->addStretch(1);
    groupLayout->addLayout(datasetBtnRow);

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

    connect(btnSeriesDownload_, &QPushButton::clicked, this, [this]{
        if (seriesRepository_->isBusy()) return;
        pbDataset_->setVisible(true);
        seriesRepository_->refreshSeriesList();
    });

    connect(seriesRepository_, &SeriesRepository::statusChanged, this, [this](const QString& s){
        lblDatasetStatus_->setVisible(!s.isEmpty());
        lblDatasetStatus_->setText(s);
    });

    connect(seriesRepository_, &SeriesRepository::updateAvailable, this,
            [this](SeriesRepository::UpdateStatus status){
                switch (status) {
                case SeriesRepository::UpdateStatus::UpdateAvailable:
                    btnSeriesDownload_->setText("Update Series List Now");
                    btnSeriesDownload_->setIcon(QIcon());
                    btnSeriesDownload_->setObjectName("actionAccent");
                    break;
                case SeriesRepository::UpdateStatus::UpToDate:
                    btnSeriesDownload_->setText("Redownload Series List");
                    btnSeriesDownload_->setIcon(QIcon());
                    btnSeriesDownload_->setObjectName("actionPrimary");
                    break;
                case SeriesRepository::UpdateStatus::Error:
                    btnSeriesDownload_->setText("Download Series List");
                    btnSeriesDownload_->setIcon(QIcon(":/icon/error.svg"));
                    btnSeriesDownload_->setObjectName("actionError");
                    break;
                }
                btnSeriesDownload_->style()->unpolish(btnSeriesDownload_);
                btnSeriesDownload_->style()->polish(btnSeriesDownload_);
            });

    connect(seriesRepository_, &SeriesRepository::seriesProgress, this,
            [this](qint64 got, qint64 total){
                double recMB = got / (1024.0*1024.0);
                pbDataset_->setVisible(true);
                lblDatasetStatus_->setVisible(true);
                if (total > 0) {
                    pbDataset_->setRange(0, 100);
                    int percent = static_cast<int>((got * 100) / total);
                    pbDataset_->setValue(percent);
                    double totalMB = total / (1024.0*1024.0);
                    lblDatasetStatus_->setText(QString("Downloading: %1 MB / %2 MB (%3%)")
                                                   .arg(recMB,0,'f',1).arg(totalMB,0,'f',1).arg(percent));
                } else if (got > 0) {
                    pbDataset_->setRange(0, 0);
                    lblDatasetStatus_->setText(QString("Downloading: %1 MB...").arg(recMB,0,'f',1));
                }
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

    btnSeriesDatasetReset_ = new QPushButton("Reset SeriesList", advWidget);
    btnSeriesDatasetReset_->setObjectName("actionError");
    btnSeriesDatasetReset_->setCursor(Qt::PointingHandCursor);
    btnSeriesDatasetReset_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    btnSeriesDatasetReset_->setMinimumHeight(36);

    btnCardDatasetReset_ = new QPushButton("Reset CardList", advWidget);
    btnCardDatasetReset_->setObjectName("actionError");
    btnCardDatasetReset_->setCursor(Qt::PointingHandCursor);
    btnCardDatasetReset_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    btnCardDatasetReset_->setMinimumHeight(36);

    btnPurgeFallback_ = new QPushButton("Purge Fallbacks", advWidget);
    btnPurgeFallback_->setObjectName("actionError");
    btnPurgeFallback_->setCursor(Qt::PointingHandCursor);
    btnPurgeFallback_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    btnPurgeFallback_->setMinimumHeight(36);

    auto* resetDatasetBtnRow = new QHBoxLayout();
    resetDatasetBtnRow->setContentsMargins(0, 0, 0, 0);
    resetDatasetBtnRow->addWidget(btnSeriesDatasetReset_);
    resetDatasetBtnRow->addWidget(btnCardDatasetReset_);
    resetDatasetBtnRow->addWidget(btnPurgeFallback_);
    resetDatasetBtnRow->addStretch(1);

    advForm->addRow(resetDatasetBtnRow);

    connect(btnSeriesDatasetReset_, &QPushButton::clicked, this, [this]{
        seriesRepository_->resetSeries();
    });

    connect(btnCardDatasetReset_, &QPushButton::clicked, this, [this]{
        seriesRepository_->resetCards();
    });

    connect(btnPurgeFallback_, &QPushButton::clicked, this, [this]{
        seriesRepository_->purgeFallbackCards();
    });

    connect(seriesRepository_, &SeriesRepository::fallbackCardsPurged, this, [this](int n){
        QMessageBox::information(this, "Purge complete",
                                 QString("Removed %1 fallback card(s). They will re-fetch from EncoreDecks "
                                         "if available next time they're viewed.").arg(n));
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

    connect(advToggle, &QPushButton::toggled, this, [advToggle, advWidget](bool on){
        advWidget->setVisible(on);
        advToggle->setText(on ? "▾ Advanced settings" : "▸ Advanced settings");
    });

    outer->addStretch(1);
}