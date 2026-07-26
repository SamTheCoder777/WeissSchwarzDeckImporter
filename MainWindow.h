// MainWindow.h — Widgets shell (canvas + toolbar) with a QML results panel.
#pragma once

#include <QMainWindow>
#include <QVector>
#include <memory>
#include <opencv2/core.hpp>
#include "tcg_infer.h"
#include "Config.h"
#include "IndexSearchProxy.h"
#include "CardDatabase.h"

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

private:
    QWidget* buildSettingsPage();
    QWidget* buildFaissPage();
    QWidget* buildDetectPage();
    cv::Mat  cropForSelection(int index) const;
    void     syncSelections();
    void     pushStateToQml();

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

    // notif setting
    bool indexNotifSilent_ = false;
};
