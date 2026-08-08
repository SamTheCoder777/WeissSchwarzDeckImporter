#include "DetectionPage.h"
#include "ImageCanvas.h"
#include "CompareDialog.h"
#include "../viewmodels/Models.h"
#include "../viewmodels/UiBridge.h"
#include "../services/ModelService.h"
#include "../index/IndexCatalog.h"
#include "../database/DatabaseUtil.h"
#include "../images/CardImageProvider.h"

#include <QtWidgets>
#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QShortcut>
#include <QtConcurrent>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

static QImage matToQImage(const cv::Mat& bgr) {
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, (int)rgb.step,
                  QImage::Format_RGB888).copy();
}

cv::Mat qImageToBgrMat(const QImage &imgIn)
{
    QImage img = imgIn.convertToFormat(QImage::Format_RGB888);
    cv::Mat rgb(img.height(), img.width(), CV_8UC3,
                const_cast<uchar*>(img.bits()), img.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    return bgr.clone();
}


DetectionPage::DetectionPage(ModelService* models, DatabaseUtil* dbUtil, DatasetManager* cardListDbManager,
                             SelectionModel* selModel, UiBridge* bridge,
                             IndexCatalog* catalog, QSortFilterProxyModel* installedProxy,
                             QWidget* parent)
    : QWidget(parent),
    models_(models), dbUtil_(dbUtil), cardListDbManager_(cardListDbManager), selModel_(selModel), bridge_(bridge),
    catalog_(catalog), installedProxy_(installedProxy) {
    buildUi();

    connect(models_, &ModelService::loaded, this, [this](bool, const QString&){
        onModelLoaded(true);
    });
    candModel_->setDatabaseUtil(dbUtil_);

    auto refreshModel = [this]() {
        QSqlDatabase db = cardListDbManager_->getUiDatabase();
        if (db.isOpen()) {
            candModel_->setCardDatabase(db);
        } else {
            qWarning() << "Database unavailable; model not updated.";
        }
    };

    QElapsedTimer t; t.start();
    refreshModel();
    qDebug() << "refreshModel took" << t.elapsed() << "ms";


    connect(cardListDbManager_, &DatasetManager::readyToUse, this, refreshModel);

    // check for dataset update
    QElapsedTimer t2; t2.start();
    cardListDbManager_->checkForUpdates();
    qDebug() << "checkForUpdates took" << t2.elapsed() << "ms";

    // allow pasting images/files only on windows. Mac crashes for some reason
    #ifdef _WIN32
    QShortcut *pasteShortcut = new QShortcut(QKeySequence::Paste, this);
    connect(pasteShortcut, &QShortcut::activated, this, [this]{
        QLabel imgLabel;
        QImage pasted = handlePasteImage(&imgLabel);
        if (pasted.isNull()) return;
        if (models_->isLoading()) {
            QMessageBox::information(this, "Please wait", "The model is still loading.\nCheck status in settings.");
            return;
        }
        if (detecting_){
            QMessageBox::information(this, "Please wait", "Detection is running.");
            return;
        }
        if (!models_->retriever()) {
            QMessageBox::information(this, "No model", "Load a model in Settings first.");
            return;
        }
        QImage rgb = pasted.convertToFormat(QImage::Format_RGB888);
        if (rgb.isNull()) { QMessageBox::warning(this, "Error", "Unsupported clipboard image format."); return; }
        cv::Mat mat(rgb.height(), rgb.width(), CV_8UC3,
                    const_cast<uchar*>(rgb.bits()), rgb.bytesPerLine());
        cv::cvtColor(mat, sourceBgr_, cv::COLOR_RGB2BGR);
        if (sourceBgr_.empty()) { QMessageBox::warning(this, "Error", "Could not read image."); return; }
        canvas_->setImage(matToQImage(sourceBgr_));
        autoDets_.clear();
        if (models_->detector()) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            try { autoDets_ = models_->detector()->detect(sourceBgr_); } catch (...) {}
            QApplication::restoreOverrideCursor();
        }
        sel_.clear();
        currentSel_ = -1;
        candModel_->clear();
        pushStateToQml();
    });
    #endif
}

void DetectionPage::onModelLoaded(bool) {
    pushStateToQml();     // refresh "model ready" state in the panel
}


QImage DetectionPage::handlePasteImage(QLabel *imageLabel)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();

    QImage image;

    if (mimeData->hasImage()) {
        image = qvariant_cast<QImage>(mimeData->imageData());
    }
    else if (mimeData->hasUrls()) {
        QList<QUrl> urls = mimeData->urls();
        if (!urls.isEmpty() && urls.first().isLocalFile())
            image = QImage(urls.first().toLocalFile());
    }

    if (!image.isNull() && imageLabel)
        imageLabel->setPixmap(QPixmap::fromImage(image));

    return image;
}

void DetectionPage::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(8, 8, 8, 0);
    auto* openBtn = new QPushButton("Open image");
    rectBtn_ = new QPushButton("Rectangle");
    polyBtn_ = new QPushButton("Polygon");
    auto* undoBtn = new QPushButton("Undo (Ctrl+Z)");
    auto* clrBtn  = new QPushButton("Clear");
    auto* detBtn  = new QPushButton("Detect");
    autoBtn_  = new QPushButton("Auto Detect Card Tool");

    connect(autoBtn_, &QPushButton::clicked, this, [this](bool on){
        autoDetectMode_ = on;
        if (on) {
            rectBtn_->setChecked(false); polyBtn_->setChecked(false);
            canvas_->setMode(ImageCanvas::ClickOnly);
        } else {
            // fell back to a drawing tool
            rectBtn_->setChecked(true);
            canvas_->setMode(ImageCanvas::Rectangle);
        }
    });

    rectBtn_->setCheckable(true); polyBtn_->setCheckable(true);
    rectBtn_->setChecked(true); autoBtn_->setCheckable(true);
    for (auto* b : {openBtn, autoBtn_, rectBtn_, polyBtn_, undoBtn, clrBtn, detBtn}) bar->addWidget(b);
    bar->addStretch();
    outer->addLayout(bar);

    auto* split = new QSplitter(Qt::Horizontal);
    canvas_ = new ImageCanvas;
    split->addWidget(canvas_);

    connect(canvas_, &ImageCanvas::canvasClickedImagePoint,
            this, &DetectionPage::onCanvasClickedImagePoint);

    // QML panel
    candModel_    = new CandidateModel(this);
    cropProvider_ = new CropImageProvider;      // engine takes ownership below
    candModel_->setDatabaseUtil(dbUtil_);

    auto* qmlPanel = new QQuickWidget;
    qmlPanel->engine()->addImageProvider("crop", cropProvider_);
    qmlPanel->engine()->addImageProvider("cardcache", new CardImageProvider(dbUtil_));
    qmlPanel->rootContext()->setContextProperty("cardDatabase", dbUtil_);
    qmlPanel->rootContext()->setContextProperty("bridge",   bridge_);
    qmlPanel->rootContext()->setContextProperty("candModel", candModel_);
    qmlPanel->rootContext()->setContextProperty("selModel",  selModel_);
    qmlPanel->rootContext()->setContextProperty("catalog",          catalog_);
    qmlPanel->rootContext()->setContextProperty("installedIndexes", installedProxy_);
    qmlPanel->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qmlPanel->setSource(QUrl("qrc:/qml/ResultsPanel.qml"));
    qmlPanel->setMinimumWidth(470);
    split->addWidget(qmlPanel);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    outer->addWidget(split, 1);

    connect(openBtn, &QPushButton::clicked, this, &DetectionPage::openImage);
    connect(rectBtn_, &QPushButton::clicked, this, [this] {
        canvas_->setMode(ImageCanvas::Rectangle);
        rectBtn_->setChecked(true); polyBtn_->setChecked(false);
        autoDetectMode_ = false; autoBtn_->setChecked(false);
    });
    connect(polyBtn_, &QPushButton::clicked, this, [this] {
        canvas_->setMode(ImageCanvas::Polygon);
        polyBtn_->setChecked(true); rectBtn_->setChecked(false);
        autoDetectMode_ = false; autoBtn_->setChecked(false);
    });
    connect(undoBtn, &QPushButton::clicked, this, [this] { canvas_->undo(); });
    connect(clrBtn,  &QPushButton::clicked, this, [this] { canvas_->clearSelections(); });
    connect(detBtn,  &QPushButton::clicked, this, &DetectionPage::runDetection);

    connect(canvas_, &ImageCanvas::selectionsChanged, this, [this] {
        syncSelections();
        sortSelectionsByPosition();
        pushStateToQml();
    });
    connect(canvas_, &ImageCanvas::selectionClicked, this, &DetectionPage::showSelectionResults);
    connect(canvas_, &ImageCanvas::selectionGeometryChanged, this, [this](int i) {
        if (i >= 0 && i < sel_.size()) {
            sel_[i].cands.clear();
            sel_[i].confirmed = false;
            sel_[i].cardId.clear();
            canvas_->setSelectionState(i, false, QString());
            if (i == currentSel_) showSelectionResults(i); else pushStateToQml();
        }
    });

    connect(bridge_, &UiBridge::selectCardRequested, this, &DetectionPage::showSelectionResults);
    connect(bridge_, &UiBridge::confirmRequested,    this, &DetectionPage::confirmCandidate);
    connect(bridge_, &UiBridge::exportRequested,     this, &DetectionPage::exportDeck);
    connect(bridge_, &UiBridge::detectRequested,     this, &DetectionPage::runDetection);
    connect(bridge_, &UiBridge::quantityRequested,   this, [this](int q) {
        if (currentSel_ >= 0 && currentSel_ < sel_.size()) {
            sel_[currentSel_].qty = q;
            canvas_->setSelectionState(currentSel_, sel_[currentSel_].confirmed,
                                       QString("%1 x%2").arg(QString::fromStdString(sel_[currentSel_].cardId)).arg(q));
            pushStateToQml();
        }
    });
    connect(bridge_, &UiBridge::openCompareRequested, this, &DetectionPage::openCompareDialog);

    // arrows cycle cards; space opens compare
    auto* nextSc = new QShortcut(QKeySequence(Qt::Key_Right), this);
    connect(nextSc, &QShortcut::activated, this, [this] {
        if (sel_.isEmpty()) return;
        int n = (currentSel_ < 0) ? 0 : (currentSel_ + 1) % sel_.size();
        showSelectionResults(n);
    });
    auto* prevSc = new QShortcut(QKeySequence(Qt::Key_Left), this);
    connect(prevSc, &QShortcut::activated, this, [this] {
        if (sel_.isEmpty()) return;
        int n = (currentSel_ <= 0) ? sel_.size() - 1 : currentSel_ - 1;
        showSelectionResults(n);
    });
    auto* cmpSc = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(cmpSc, &QShortcut::activated, this, &DetectionPage::openCompareDialog);
}

void DetectionPage::forceRectangleTool() {
    canvas_->setMode(ImageCanvas::Rectangle);
    if (rectBtn_) rectBtn_->setChecked(true);
    if (polyBtn_) polyBtn_->setChecked(false);
    if (autoBtn_) autoBtn_->setChecked(false);
    autoDetectMode_ = false;
}

void DetectionPage::onCanvasClickedImagePoint(const QPointF& imgPt) {
    if (!autoDetectMode_ || autoDets_.empty()) return;

    cv::Point2f click((float)imgPt.x(), (float)imgPt.y());
    int best = -1;
    for (int i = 0; i < (int)autoDets_.size(); ++i) {
        const auto& poly = autoDets_[i].polygon;
        std::vector<cv::Point2f> cvpoly(poly.begin(), poly.end());
        if (cvpoly.size() >= 3 &&
            cv::pointPolygonTest(cvpoly, click, false) >= 0) { best = i; break; }
    }
    if (best < 0) {
        double bd = 1e18;
        for (int i = 0; i < (int)autoDets_.size(); ++i) {
            double dx = autoDets_[i].centroid.x - click.x;
            double dy = autoDets_[i].centroid.y - click.y;
            double d = dx*dx + dy*dy;
            if (d < bd) { bd = d; best = i; }
        }
        if (best >= 0) {
            double diag = std::hypot(sourceBgr_.cols, sourceBgr_.rows);
            if (std::sqrt(bd) > diag * 0.08) best = -1;
        }
    }
    if (best < 0) return;

    QPolygonF poly;
    for (const auto& p : autoDets_[best].polygon) poly << QPointF(p.x, p.y);

    for (int i = 0; i < canvas_->selectionCount(); ++i) {
        QPolygonF ex = canvas_->selection(i);
        if (ex.containsPoint(QPointF(autoDets_[best].centroid.x,
                                     autoDets_[best].centroid.y), Qt::OddEvenFill)) {
            canvas_->setHighlight(i);
            showSelectionResults(i);
            return;
        }
    }

    canvas_->addQuadSelection(poly);
    syncSelections();
    pushStateToQml();
}

void DetectionPage::detectCards() {
    if (!models_->detector()) {
        QMessageBox::information(this, "No detector",
                                 "Set the YOLO detector .onnx in Settings and press Load.");
        return;
    }
    if (sourceBgr_.empty()) {
        QMessageBox::information(this, "No image", "Open an image first.");
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    std::vector<CardDetection> dets = models_->detector()->detect(sourceBgr_);
    QApplication::restoreOverrideCursor();

    for (const auto& d : dets) {
        QPolygonF poly;
        for (const auto& p : d.quad) poly << QPointF(p.x, p.y);
        canvas_->addQuadSelection(poly);
    }
    syncSelections();
    pushStateToQml();
}

void DetectionPage::openCompareDialog() {
    if (currentSel_ < 0 || currentSel_ >= sel_.size()) return;
    const auto& cands = sel_[currentSel_].cands;
    if (cands.empty()) return;

    cv::Mat crop = cropForSelection(currentSel_);
    QImage cropImg = crop.empty() ? QImage() : matToQImage(crop);

    int start = 0;
    if (!sel_[currentSel_].cardId.empty())
        for (int i = 0; i < (int)cands.size(); ++i)
            if (cands[i].card_id == sel_[currentSel_].cardId) { start = i; break; }

    QSqlDatabase db;   // CompareDialog signature kept; pass a default db (uses dbUtil_ for images)
    CompareDialog dlg(cropImg, cands, start, db, dbUtil_, this);
    if (dlg.exec() == QDialog::Accepted) {
        int idx = dlg.confirmedIndex();
        if (idx >= 0) confirmCandidate(idx);
    }
}

void DetectionPage::syncSelections() {
    int curId = (currentSel_ >= 0 && currentSel_ < sel_.size()) ? sel_[currentSel_].id : -1;

    QVector<SelState> next;
    next.reserve(canvas_->selectionCount());
    for (int i = 0; i < canvas_->selectionCount(); ++i) {
        int id = canvas_->selectionId(i);
        int found = -1;
        for (int j = 0; j < sel_.size(); ++j) if (sel_[j].id == id) { found = j; break; }
        if (found >= 0) next.push_back(sel_[found]);
        else { SelState s; s.id = id; next.push_back(s); }
    }
    sel_ = next;

    currentSel_ = -1;
    if (curId >= 0)
        for (int i = 0; i < sel_.size(); ++i)
            if (sel_[i].id == curId) { currentSel_ = i; break; }
}

void DetectionPage::sortSelectionsByPosition() {
    int n = canvas_->selectionCount();
    if (n < 2 || sourceBgr_.empty()) return;

    auto centroid = [&](int i) {
        QPolygonF p = canvas_->selection(i);
        QPointF c;
        for (const QPointF& q : p) c += q;
        return c / p.size();
    };

    struct Item { int idx; double x, y; };
    std::vector<Item> items;
    items.reserve(n);
    for (int i = 0; i < n; ++i) {
        QPointF c = centroid(i);
        items.push_back({i, c.x(), c.y()});
    }
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b){ return a.y < b.y; });

    double avgCardH = 0;
    for (int i = 0; i < n; ++i)
        avgCardH += canvas_->selection(i).boundingRect().height();
    avgCardH /= n;
    double rowTol = std::max(1.0, avgCardH * 0.5);

    std::vector<std::vector<Item>> rows;
    std::vector<double> rowMeanY;
    for (const Item& it : items) {
        if (rows.empty() || (it.y - rowMeanY.back()) > rowTol) {
            rows.push_back({it});
            rowMeanY.push_back(it.y);
        } else {
            rows.back().push_back(it);
            double s = 0;
            for (const Item& c : rows.back()) s += c.y;
            rowMeanY.back() = s / rows.back().size();
        }
    }
    for (auto& row : rows)
        std::sort(row.begin(), row.end(),
                  [](const Item& a, const Item& b){ return a.x < b.x; });

    QVector<int> order;
    order.reserve(n);
    for (const auto& row : rows)
        for (const Item& it : row)
            order.push_back(it.idx);

    bool identity = true;
    for (int i = 0; i < n; ++i)
        if (order[i] != i) { identity = false; break; }
    if (identity) return;

    canvas_->reorder(order);
    QVector<SelState> reordered;
    reordered.reserve(n);
    for (int idx : order) reordered.push_back(sel_[idx]);
    sel_ = reordered;
    currentSel_ = -1;
}

void DetectionPage::pushStateToQml() {
    QVector<SelectionModel::Row> rows;
    int confirmed = 0, total = 0;
    for (const auto& s : sel_) {
        SelectionModel::Row r;
        r.confirmed = s.confirmed;
        r.qty       = s.qty;
        r.label     = s.confirmed
                      ? QString("%1  x%2").arg(QString::fromStdString(s.cardId)).arg(s.qty)
                      : QStringLiteral("(not confirmed)");
        r.cardId    = QString::fromStdString(s.cardId);
        rows.push_back(r);
        if (s.confirmed) { ++confirmed; total += s.qty; }
    }
    selModel_->setRows(rows);

    QString summary = QString("%1 selected · %2 confirmed · %3 cards")
                          .arg(sel_.size()).arg(confirmed).arg(total);
    bool isConf = (currentSel_ >= 0 && currentSel_ < sel_.size() && sel_[currentSel_].confirmed);
    QString confText = isConf ? QString("Confirmed: %1").arg(QString::fromStdString(sel_[currentSel_].cardId))
                              : QStringLiteral("Not confirmed");
    int qty = (currentSel_ >= 0 && currentSel_ < sel_.size()) ? sel_[currentSel_].qty : 1;
    bridge_->setState(summary, confText, isConf, qty, currentSel_, models_->retriever() != nullptr);
}

void DetectionPage::openImage() {
    if (models_->isLoading()) {
        QMessageBox::information(this, "Please wait", "The model is still loading.\nCheck status in settings.");
        return;
    }
    if (detecting_){
        QMessageBox::information(this, "Please wait", "Detection is running.");
        return;
    }
    if (!models_->retriever()) {
        QMessageBox::information(this, "No model", "Load a model in Settings first.");
        return;
    }
    QString p = QFileDialog::getOpenFileName(this, "Open image", {},
                                             "Images (*.png *.jpg *.jpeg *.webp *.bmp)");
    if (p.isEmpty()) return;
    sourceBgr_ = cv::imread(p.toStdString(), cv::IMREAD_COLOR);
    if (sourceBgr_.empty()) { QMessageBox::warning(this, "Error", "Could not read image."); return; }
    sourcePath_ = p;
    canvas_->setImage(matToQImage(sourceBgr_));
    autoDets_.clear();
    if (models_->detector()) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        try { autoDets_ = models_->detector()->detect(sourceBgr_); } catch (...) {}
        QApplication::restoreOverrideCursor();
    }
    sel_.clear();
    currentSel_ = -1;
    candModel_->clear();
    pushStateToQml();
}

cv::Mat DetectionPage::cropForSelection(int index) const {
    QPolygonF poly = canvas_->selection(index);
    if (poly.isEmpty() || sourceBgr_.empty()) return {};

    if (poly.size() == 4) {
        std::vector<cv::Point2f> p;
        for (const QPointF& q : poly) p.emplace_back((float)q.x(), (float)q.y());
        cv::Point2f c(0.f, 0.f);
        for (auto& q : p) c += q;
        c *= 1.0f / (float)p.size();
        std::vector<cv::Point2f> src(4);
        for (auto& q : p) {
            if      (q.x <  c.x && q.y <  c.y) src[0] = q;
            else if (q.x >= c.x && q.y <  c.y) src[1] = q;
            else if (q.x >= c.x && q.y >= c.y) src[2] = q;
            else                               src[3] = q;
        }
        float wTop = (float)cv::norm(src[1] - src[0]), wBot = (float)cv::norm(src[2] - src[3]);
        float hL   = (float)cv::norm(src[3] - src[0]), hR   = (float)cv::norm(src[2] - src[1]);
        int W = (int)std::lround(std::max(wTop, wBot));
        int H = (int)std::lround(std::max(hL, hR));
        if (W > 8 && H > 8) {
            std::vector<cv::Point2f> dst{{0.f,0.f}, {(float)W-1,0.f},
                                         {(float)W-1,(float)H-1}, {0.f,(float)H-1}};
            cv::Mat M = cv::getPerspectiveTransform(src, dst);
            cv::Mat warped;
            cv::warpPerspective(sourceBgr_, warped, M, cv::Size(W, H),
                                cv::INTER_CUBIC, cv::BORDER_CONSTANT, cv::Scalar(114,114,114));
            return warped;
        }
    }

    QRectF bb = poly.boundingRect();
    int x0 = std::max(0, (int)std::floor(bb.left()));
    int y0 = std::max(0, (int)std::floor(bb.top()));
    int x1 = std::min(sourceBgr_.cols, (int)std::ceil(bb.right()));
    int y1 = std::min(sourceBgr_.rows, (int)std::ceil(bb.bottom()));
    if (x1 <= x0 || y1 <= y0) return {};
    cv::Mat crop = sourceBgr_(cv::Rect(x0, y0, x1 - x0, y1 - y0)).clone();

    std::vector<cv::Point> pts;
    for (const QPointF& q : poly)
        pts.emplace_back((int)std::lround(q.x() - x0), (int)std::lround(q.y() - y0));
    cv::Mat mask(crop.size(), CV_8UC1, cv::Scalar(0));
    std::vector<std::vector<cv::Point>> polys{pts};
    cv::fillPoly(mask, polys, cv::Scalar(255));
    cv::Mat out(crop.size(), crop.type(), cv::Scalar(114, 114, 114));
    crop.copyTo(out, mask);
    return out;
}

void DetectionPage::runDetection() {
    if (models_->isLoading()) {
        QMessageBox::information(this, "Please wait", "The model is still loading.\nCheck status in settings.");
        return;
    }
    if (detecting_){
        QMessageBox::information(this, "Please wait", "Detection already running.");
        return;
    }
    if (!models_->retriever()) {
        QMessageBox::information(this, "No model", "Load a model in Settings first."); return;
    }
    syncSelections();
    if (sel_.isEmpty()) { QMessageBox::information(this, "No selection", "Select at least one card."); return; }

    forceRectangleTool();
    detecting_ = true;

    auto crops = std::make_shared<std::vector<cv::Mat>>();
    for (int i = 0; i < sel_.size(); ++i)
        crops->push_back(sel_[i].confirmed ? cv::Mat() : cropForSelection(i));

    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto results = std::make_shared<std::vector<std::vector<Candidate>>>(sel_.size());

    connect(&detectWatcher_, &QFutureWatcher<void>::finished, this, [this, results] {
        for (int i = 0; i < sel_.size() && i < (int)results->size(); ++i)
            if (!(*results)[i].empty()) sel_[i].cands = (*results)[i];
        QApplication::restoreOverrideCursor();
        if (!sel_.isEmpty()) { showSelectionResults(0); }
        pushStateToQml();
        detecting_ = false;
    }, Qt::SingleShotConnection);

    ModelService* models = models_;
    QFuture<void> fut = QtConcurrent::run([models, crops, results] {
        for (size_t i = 0; i < crops->size(); ++i)
            if (!(*crops)[i].empty())
                (*results)[i] = models->retriever()->search((*crops)[i], 15);
    });
    detectWatcher_.setFuture(fut);
}

void DetectionPage::showSelectionResults(int index) {
    if (index < 0 || index >= sel_.size()) return;
    currentSel_ = index;
    canvas_->setHighlight(index);

    cv::Mat crop = cropForSelection(index);
    cropProvider_->setImage(crop.empty() ? QImage() : matToQImage(crop));
    bridge_->bumpCrop();

    candModel_->setCandidates(sel_[index].cands, sel_[index].cardId);
    pushStateToQml();
}

void DetectionPage::confirmCandidate(int candIndex) {
    if (currentSel_ < 0 || currentSel_ >= sel_.size()) return;
    auto& s = sel_[currentSel_];
    if (candIndex < 0 || candIndex >= (int)s.cands.size()) return;

    s.confirmed = true;
    s.cardId    = s.cands[candIndex].card_id;

    canvas_->setSelectionState(currentSel_, true,
                               QString("%1 x%2").arg(QString::fromStdString(s.cardId)).arg(s.qty));
    candModel_->setConfirmedId(s.cardId);
    pushStateToQml();
}

void DetectionPage::exportDeck() {
    QStringList lines;
    int missing = 0;
    for (const auto& s : sel_) {
        if (!s.confirmed) { ++missing; continue; }
        for (int k = 0; k < s.qty; ++k) lines << QString::fromStdString(s.cardId);
    }
    if (lines.isEmpty()) {
        QMessageBox::information(this, "Nothing to export", "Confirm at least one card first.");
        return;
    }
    if (missing > 0 &&
        QMessageBox::question(this, "Unconfirmed cards",
                              QString("%1 selection(s) are not confirmed and will be skipped.\nExport anyway?")
                                  .arg(missing)) != QMessageBox::Yes) return;

    QString p = QFileDialog::getSaveFileName(this, "Export deck list", "deck.txt", "Text (*.txt)");
    if (p.isEmpty()) return;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Error", "Could not write file."); return;
    }
    QTextStream out(&f);
    for (const QString& l : lines) out << l << "\n";
    f.close();
    QMessageBox::information(this, "Exported",
                             QString("Wrote %1 cards to:\n%2").arg(lines.size()).arg(p));
}