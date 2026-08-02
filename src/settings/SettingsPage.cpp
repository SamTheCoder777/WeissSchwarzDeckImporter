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

SettingsPage::SettingsPage(ModelService* models, DatasetManager *dbManager, QWidget *parent): QWidget(parent),
    models_(models), dbManager_(dbManager)
{
    buildUi();
}

void SettingsPage::buildUi()
{
    auto* w = new QWidget(this);
    auto* form = new QFormLayout(w);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(w);

    QGroupBox *modelGroup = new QGroupBox("Model Paths", this);
    QFormLayout *modelForm = new QFormLayout(modelGroup);

    auto browseRow = [this, modelForm, modelGroup](QLineEdit*& edit, const QString& label, bool dir) {
        edit = new QLineEdit(modelGroup);
        auto* btn = new QPushButton("Browse…", modelGroup);
        auto* row = new QHBoxLayout;
        row->addWidget(edit);
        row->addWidget(btn);
        modelForm->addRow(label, row);
        connect(btn, &QPushButton::clicked, this, [this, edit, dir] {
            QString p = dir ? QFileDialog::getExistingDirectory(this, "Select folder")
                            : QFileDialog::getOpenFileName(this, "Select file");
            if (!p.isEmpty()) edit->setText(p);
        });
    };
    browseRow(onnxEdit_, "ONNX model (.onnx):", false);
    browseRow(yoloEdit_, "YOLO detector (.onnx):", false);

    // Try to load models
    bool modelPathLoaded = !Config::instance().getCurModelPath().isNull() && !Config::instance().getCurModelPath().isEmpty();
    onnxEdit_->setText(modelPathLoaded ? Config::instance().getCurModelPath() : "");
    bool yoloModelPathLoaded = !Config::instance().getCurYoloModelPath().isNull() && !Config::instance().getCurYoloModelPath().isEmpty();
    yoloEdit_->setText(yoloModelPathLoaded ? Config::instance().getCurYoloModelPath() : "");
    if (modelPathLoaded && yoloModelPathLoaded) {
        QTimer::singleShot(0, this, [this]() {
            QElapsedTimer t; t.start();
            models_->load(true);
            qDebug() << "load took" << t.elapsed() << "ms";
        });
    }

    auto* loadBtn = new QPushButton("Load models", modelGroup);
    modelForm->addRow("", loadBtn);
    connect(loadBtn, &QPushButton::clicked, this, [this]{
        if(models_->isLoading()) return;
        models_->load(onnxEdit_->text(),
                      models_->getIndexDir(), yoloEdit_->text(),
                      Config::instance().getModelNative(), Config::instance().getModelImgSize(), false);
    });

    modelStatus_ = new QLabel("No model loaded.", modelGroup);
    modelStatus_->setWordWrap(true);
    modelForm->addRow("Status:", modelStatus_);

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


    form->addRow(modelGroup);

    // Dataset settings

    QGroupBox *datasetGroup = new QGroupBox("Dataset Maintenance", this);
    QVBoxLayout *groupLayout = new QVBoxLayout(datasetGroup);

    lblDatasetStatus_ = new QLabel("", datasetGroup);

    pbDataset_ = new QProgressBar(datasetGroup);
    pbDataset_->setRange(0, 100);
    pbDataset_->setValue(0);
    pbDataset_->setTextVisible(true);

    btnDatasetAction_ = new QPushButton("Check for Dataset Updates", datasetGroup);

    groupLayout->addWidget(lblDatasetStatus_);
    groupLayout->addWidget(pbDataset_);
    groupLayout->addWidget(btnDatasetAction_);

    form->addRow(datasetGroup);

    connect(btnDatasetAction_, &QPushButton::clicked, this, [this]{
        if(!dbManager_) return;

        if(dbUpdateNeeded_){
            dbManager_->startDownloadAndImport();
        }else{
            dbManager_->checkAndLoad(true);
        }
    });

    connect(dbManager_, &DatasetManager::statusChanged, this, [this](const QString &statusText) {
        qDebug()<<statusText;
        lblDatasetStatus_->setText(statusText);
    });

    connect(dbManager_, &DatasetManager::updateAvailable, this, [this](bool available, const QString &newVer) {
        if (available) {
            btnDatasetAction_->setText("Update Dataset Now");
        }else{
            btnDatasetAction_->setText("Redownload Dataset");
        }});

    connect(dbManager_, &DatasetManager::downloadProgress, this, [this](qint64 bytesReceived, qint64 bytesTotal) {
        double recMB = bytesReceived / (1024.0 * 1024.0);

        if (bytesTotal > 0) {
            pbDataset_->setRange(0, 100);
            int percent = static_cast<int>((bytesReceived * 100) / bytesTotal);
            pbDataset_->setValue(percent);

            double totalMB = bytesTotal / (1024.0 * 1024.0);
            lblDatasetStatus_->setText(QString("Downloading: %1 MB / %2 MB (%3%)")
                                           .arg(recMB, 0, 'f', 1)
                                           .arg(totalMB, 0, 'f', 1)
                                           .arg(percent));
        } else if (bytesReceived > 0) {
            pbDataset_->setRange(0, 0);
            lblDatasetStatus_->setText(QString("Downloading: %1 MB...").arg(recMB, 0, 'f', 1));
        }
    });


    // Advanced settings
    auto* advToggle = new QPushButton("▸ Advanced settings");
    advToggle->setCheckable(true);
    advToggle->setStyleSheet(
        "QPushButton{ text-align:left; border:none; color:#9aa0a6;"
        " padding:6px 0; background:transparent; }"
        "QPushButton:hover{ color:#e8eaed; }");
    form->addRow(advToggle);

    auto* advWidget = new QWidget;
    auto* advForm = new QFormLayout(advWidget);
    advForm->setContentsMargins(12, 4, 0, 4);

    imgSizeSpin_ = new QSpinBox;
    imgSizeSpin_->setRange(64, 1024);
    imgSizeSpin_->setSingleStep(16);
    imgSizeSpin_->setValue(336);
    advForm->addRow("Image size:", imgSizeSpin_);

    nativeCheck_ = new QCheckBox("native aspect");
    nativeCheck_->setChecked(true);
    advForm->addRow("", nativeCheck_);

    advWidget->setVisible(false);
    form->addRow(advWidget);

    connect(advToggle, &QPushButton::toggled, this, [advToggle, advWidget](bool on){
        advWidget->setVisible(on);
        advToggle->setText(on ? "▾ Advanced settings" : "▸ Advanced settings");
    });
}
