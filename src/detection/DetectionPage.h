#pragma once

#include "ImageCanvas.h"
#include "../viewmodels/Models.h"
#include "../viewmodels/UiBridge.h"
#include "../index/IndexCatalog.h"
#include "../services/ModelService.h"

#include <QPushButton>
#include <QQuickWidget>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QWidget>
#include <QFutureWatcher>
#include <QLabel>

class DetectionPage: public QWidget {
    Q_OBJECT

public:
    explicit DetectionPage(ModelService* models, DatabaseUtil* dbUtil,
                           SelectionModel* selModel, UiBridge* bridge,
                           IndexCatalog* catalog, QSortFilterProxyModel* installedProxy,
                           QWidget* parent = nullptr);

private:
    ModelService* models_;
    DatabaseUtil* dbUtil_;
    SelectionModel* selModel_;
    UiBridge* bridge_;
    IndexCatalog* catalog_;
    QSortFilterProxyModel* installedProxy_;

    QPushButton *rectBtn_;
    QPushButton *polyBtn_;
    QPushButton *autoBtn_;

    std::vector<CardDetection> autoDets_;
    bool autoDetectMode_ = false;
    ImageCanvas *canvas_;

    CandidateModel *candModel_;
    CropImageProvider *cropProvider_;

    cv::Mat  sourceBgr_;
    QString  sourcePath_;

    QMap<int, int> selRotations_;

    QFutureWatcher<void> detectWatcher_;

    struct SelState {
        std::vector<Candidate> cands;
        bool        confirmed = false;
        std::string cardId;
        int         qty = 1;
        int id = -1;
        int rotation = 0;
    };

    QVector<SelState> sel_;
    int      currentSel_ = -1;

    bool detecting_ = false;

    void onModelLoaded(bool);
    void buildUi();
    void forceRectangleTool();
    void onCanvasClickedImagePoint(const QPointF &imgPt);
    void detectCards();
    void openCompareDialog();
    void syncSelections();
    void sortSelectionsByPosition();
    void pushStateToQml();
    void openImage();
    cv::Mat cropForSelection(int index) const;
    void runDetection();
    void showSelectionResults(int index);
    void rotateSelectionImage(int index, int rot);
    void confirmCandidate(int candIndex);
    void exportDeck();
    QImage handlePasteImage();
};


