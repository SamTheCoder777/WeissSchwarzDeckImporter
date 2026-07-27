// MainWindow.h — Widgets shell (canvas + toolbar) with a QML results panel.
#pragma once

#include <QFutureWatcher>
#include <QMainWindow>
#include <QPushButton>
#include <QVector>
#include <memory>
#include <opencv2/core.hpp>
#include "tcg_infer.h"
#include "Config.h"
#include "IndexSearchProxy.h"
#include "CardDatabase.h"
#include "CardDetector.h"

class ImageCanvas;
class CandidateModel;
class SelectionModel;
class CropImageProvider;
class UiBridge;
class IndexCatalog;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QLabel;
class QStackedWidget;
class QQuickWidget;
class QSortFilterProxyModel;

QString toDeckCode(const std::string& cardId);   // defined in Models.cpp

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void loadModel();
    void openImage();
    void runDetection();
    void showSelectionResults(int index);
    void confirmCandidate(int candIndex);
    void exportDeck();
    void detectCards();                     // run YOLO, add all card quads
    void onCanvasClickedImagePoint(const QPointF& imgPt);   // claim the card under a click

private:
    QWidget* buildSettingsPage();
    QWidget* buildFaissPage();
    QWidget* buildDetectPage();
    cv::Mat  cropForSelection(int index) const;
    void     syncSelections();
    void     pushStateToQml();
    void sortSelectionsByPosition();
    void forceRectangleTool();

    struct SelState {
        std::vector<Candidate> cands;
        bool        confirmed = false;
        std::string cardId;
        int         qty = 1;
    };

    std::unique_ptr<TCGRetriever> retriever_;
    cv::Mat  sourceBgr_;
    QString  sourcePath_;
    QVector<SelState> sel_;
    int      currentSel_ = -1;
    std::unique_ptr<CardDetector> detector_;
    QLineEdit* yoloEdit_ = nullptr;         // Settings: path to best.onnx
    std::vector<CardDetection> autoDets_;           // cached detections for current image
    bool autoDetectMode_ = false;                   // is the tool active?

    // buttons
    QPushButton* rectBtn_ = nullptr;
    QPushButton* polyBtn_ = nullptr;
    QPushButton* autoBtn_ = nullptr;

    // settings widgets
    QLineEdit* onnxEdit_;
    QLineEdit* indexEdit_;
    QLineEdit* mastersEdit_;
    QSpinBox*  imgSizeSpin_;
    QCheckBox* nativeCheck_;
    QLabel*    modelStatus_;

    // detection
    QStackedWidget* pages_;
    ImageCanvas*    canvas_;
    QQuickWidget*   qmlPanel_;
    CandidateModel* candModel_;
    CardDatabase* db_ = nullptr;
    SelectionModel* selModel_;
    CropImageProvider* cropProvider_;
    UiBridge*       bridge_;
    IndexCatalog*   catalog_ = nullptr;
    QSortFilterProxyModel* installedProxy_ = nullptr;
    IndexSearchProxy* searchProxy_ = nullptr;
    QFutureWatcher<void> detectWatcher_;

    // notif setting
    bool indexNotifSilent_ = false;
};
