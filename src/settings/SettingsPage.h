#pragma once

#include "../database/DatasetManager.h"
#include "../services/ModelService.h"
#include "../cardsIndex/SeriesRepository.h"

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
    explicit SettingsPage(ModelService* models, SeriesRepository* seriesRepository_,
                          QWidget* parent = nullptr);

private:
    void buildUi();

    SeriesRepository* seriesRepository_;
    ModelService* models_;

    // settings widgets
    QLineEdit* onnxEdit_;
    QLineEdit* yoloEdit_;
    QSpinBox*  imgSizeSpin_;
    QCheckBox* nativeCheck_;
    QLabel*    modelStatus_;

    // settings dataset
    QPushButton *btnSeriesDownload_;
    QLabel *lblDatasetStatus_;
    QPushButton *btnSeriesDatasetReset_;
    QPushButton *btnCardDatasetReset_;
    QPushButton *btnPurgeFallback_;
    QProgressBar *pbDataset_;

    DatasetManager::UpdateStatus dbUpdateStatus_;
};