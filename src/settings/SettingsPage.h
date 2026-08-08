#pragma once

#include "../database/DatasetManager.h"
#include "../services/ModelService.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

class SettingsPage: public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(ModelService* models, DatasetManager* cardListDbManager, DatasetManager* seriesListDbManager,
                          QWidget* parent = nullptr);

private:
    void buildUi();

    DatasetManager* cardListDbManager_;
    DatasetManager* seriesListDbManager_;
    ModelService* models_;

    // settings widgets
    QLineEdit* onnxEdit_;
    QLineEdit* yoloEdit_;
    QSpinBox*  imgSizeSpin_;
    QCheckBox* nativeCheck_;
    QLabel*    modelStatus_;

    // settings dataset
    QLabel *lblDatasetStatus_;
    QPushButton *btnDatasetAction_;
    QProgressBar *pbDataset_;

    DatasetManager::UpdateStatus dbUpdateStatus_;
};