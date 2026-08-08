#include "SettingsPage.h"
#include "../core/Config.h"

#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QStyle>

SettingsPage::SettingsPage(ModelService* models, DatasetManager *cardListDbManager, DatasetManager *seriesListDbManager,
                           QWidget *parent): QWidget(parent),
    models_(models), cardListDbManager_(cardListDbManager), seriesListDbManager_(seriesListDbManager)
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
    browseRow(onnxEdit_, "ONNX model", false);
    browseRow(yoloEdit_, "YOLO detector", false);

    bool modelPathLoaded = !Config::instance().getCurModelPath().isNull()
                        && !Config::instance().getCurModelPath().isEmpty();
    onnxEdit_->setText(modelPathLoaded ? Config::instance().getCurModelPath() : "");
    bool yoloModelPathLoaded = !Config::instance().getCurYoloModelPath().isNull()
                            && !Config::instance().getCurYoloModelPath().isEmpty();
    yoloEdit_->setText(yoloModelPathLoaded ? Config::instance().getCurYoloModelPath() : "");

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

    outer->addWidget(modelGroup);

    connect(loadBtn, &QPushButton::clicked, this, [this]{
        if(models_->isLoading()) return;
        models_->load(onnxEdit_->text(),
                      models_->getIndexDir(), yoloEdit_->text(),
                      Config::instance().getModelNative(), Config::instance().getModelImgSize(), false);
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

    // dataset maintenance
    auto* datasetGroup = new QFrame(this);
    datasetGroup->setObjectName("sectionCard");

    auto* groupLayout = new QVBoxLayout(datasetGroup);
    groupLayout->setContentsMargins(20, 20, 20, 20);
    groupLayout->setSpacing(16);

    auto* datasetHeading = new QLabel("Cards Series Dataset Maintenance", datasetGroup);
    datasetHeading->setObjectName("sectionHeading");
    groupLayout->addWidget(datasetHeading);

    btnDatasetAction_ = new QPushButton("Check for updates", datasetGroup);
    btnDatasetAction_->setObjectName("actionPrimary");
    btnDatasetAction_->setCursor(Qt::PointingHandCursor);
    btnDatasetAction_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    btnDatasetAction_->setMinimumHeight(36);

    auto* datasetBtnRow = new QHBoxLayout();
    datasetBtnRow->setContentsMargins(0, 0, 0, 0);
    datasetBtnRow->addWidget(btnDatasetAction_);
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

    connect(btnDatasetAction_, &QPushButton::clicked, this, [this]{
        if (!seriesListDbManager_ || seriesListDbManager_->isDownloading()) return;
        pbDataset_->setVisible(true);
        if (dbUpdateStatus_ == DatasetManager::UpdateStatus::UpdateAvailable) seriesListDbManager_->startDownloadAndImport();
        else                 seriesListDbManager_->checkAndLoad(true);
    });

    connect(seriesListDbManager_, &DatasetManager::statusChanged, this, [this](const QString& s){
        lblDatasetStatus_->setVisible(!s.isEmpty());
        lblDatasetStatus_->setText(s);
    });

    connect(seriesListDbManager_, &DatasetManager::updateAvailable, this, [this](DatasetManager::UpdateStatus status , const QString&){
        dbUpdateStatus_ = status;

        switch (status) {
            case DatasetManager::UpdateStatus::UpdateAvailable:
                btnDatasetAction_->setText("Update Dataset Now");
                btnDatasetAction_->setIcon(QIcon());
                btnDatasetAction_->setObjectName("actionAccent");
                break;
            case DatasetManager::UpdateStatus::UpToDate:
                btnDatasetAction_->setText("Redownload Dataset");
                btnDatasetAction_->setIcon(QIcon());
                btnDatasetAction_->setObjectName("actionPrimary");
                break;
            case DatasetManager::UpdateStatus::Error:
                btnDatasetAction_->setText("Redownload Dataset");
                btnDatasetAction_->setIcon(QIcon(":/icon/error.svg"));
                btnDatasetAction_->setObjectName("actionError");
                break;
        }

        btnDatasetAction_->style()->unpolish(btnDatasetAction_);
        btnDatasetAction_->style()->polish(btnDatasetAction_);
    });

    connect(seriesListDbManager_, &DatasetManager::downloadProgress, this, [this](qint64 got, qint64 total){
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

    imgSizeSpin_ = new QSpinBox;
    imgSizeSpin_->setRange(64, 1024);
    imgSizeSpin_->setSingleStep(16);
    imgSizeSpin_->setValue(336);
    imgSizeSpin_->setFixedWidth(120);
    imgSizeSpin_->setMinimumHeight(32);
    advForm->addRow("Image size", imgSizeSpin_);

    nativeCheck_ = new QCheckBox("Native aspect ratio");
    nativeCheck_->setChecked(true);
    advForm->addRow("", nativeCheck_);

    advWidget->setVisible(false);
    outer->addWidget(advWidget);

    connect(advToggle, &QPushButton::toggled, this, [advToggle, advWidget](bool on){
        advWidget->setVisible(on);
        advToggle->setText(on ? "▾ Advanced settings" : "▸ Advanced settings");
    });

    outer->addStretch(1);
}