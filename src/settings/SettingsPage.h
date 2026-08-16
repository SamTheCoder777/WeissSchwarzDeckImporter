#pragma once

#include "../database/DatasetManager.h"
#include "../services/ModelService.h"
#include "../cardsIndex/SeriesRepository.h"
#include "../index/IndexCatalog.h"

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
    explicit SettingsPage(ModelService* models, SeriesRepository* seriesRepository_, IndexCatalog* indexCatalog,
                          QWidget* parent = nullptr);

private:
    void buildUi();

    SeriesRepository* seriesRepository_;
    ModelService* models_;
    IndexCatalog* indexCatalog_;

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
    QLineEdit *manifestUrlEdit_;
    QLineEdit *indexPathEdit_;

    DatasetManager::UpdateStatus dbUpdateStatus_;
};