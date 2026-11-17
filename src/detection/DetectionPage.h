#pragma once

#include "../index/IndexCatalog.h"
#include "../services/ModelService.h"
#include "../viewmodels/Models.h"
#include "../viewmodels/UiBridge.h"
#include "ImageCanvas.h"

#include <QFutureWatcher>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QQuickWidget>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QWidget>

class DetectionPage : public QWidget
{
    Q_OBJECT

public:
    explicit DetectionPage(ModelService *models,
                           DatabaseUtil *dbUtil,
                           SelectionModel *selModel,
                           UiBridge *bridge,
                           IndexCatalog *catalog,
                           QSortFilterProxyModel *installedProxy,
                           QWidget *parent = nullptr);

    Q_INVOKABLE void openOfficialCard(const QString &cardCode);
    Q_INVOKABLE void openCompareAt(int candidateIndex);

private:
    ModelService *models_;
    DatabaseUtil *dbUtil_;
    SelectionModel *selModel_;
    UiBridge *bridge_;
    IndexCatalog *catalog_;
    QSortFilterProxyModel *installedProxy_;
    QSortFilterProxyModel *installedSearchProxy_;

    QPushButton *rectBtn_;
    QPushButton *polyBtn_;
    QPushButton *autoBtn_;
    QPushButton *handBtn_;

    std::vector<CardDetection> autoDets_;
    bool autoDetectMode_ = false;
    ImageCanvas *canvas_;

    CandidateModel *candModel_;
    CropImageProvider *cropProvider_;

    cv::Mat sourceBgr_;
    QString sourcePath_;
    QImage pendingImage_;
    QString pendingImagePath_;

    QMap<int, int> selRotations_;

    QFutureWatcher<void> detectWatcher_;

    struct SelState
    {
        std::vector<Candidate> cands;
        bool confirmed = false;
        std::string cardId;
        int qty = 1;
        int id = -1;
        int rotation = 0;
        QString filterSummary;
    };

    QVector<SelState> sel_;
    int currentSel_ = -1;

    bool detecting_ = false;

    void requestImage(const QImage &image, const QString &path = QString());
    void finishPendingImage();

    void onModelLoaded(bool);
    void buildUi();
    void forceHandTool();
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
    void confirmCandidateCode(const QString &code);
    void exportDeck();
    QImage handlePasteImage();
    void runFilteredSearch(const QVariantMap &filters, const QString &summary);
    void clearFilteredSearch();
    void redetectSelection(int i);
    int canvasIndexFor(int selIdx) const;
};
