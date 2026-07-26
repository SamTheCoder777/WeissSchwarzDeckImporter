// MainWindow.cpp — Widgets shell + QML results panel.
#include "MainWindow.h"
#include "ImageCanvas.h"
#include "Models.h"
#include "UiBridge.h"
#include "IndexCatalog.h"

#include <QtWidgets>
#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSortFilterProxyModel>
#include <QRegularExpression>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

static QImage matToQImage(const cv::Mat& bgr) {
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, (int)rgb.step,
                  QImage::Format_RGB888).copy();
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
     // Faiss catalog
    catalog_ = new IndexCatalog(this);

    searchProxy_ = new IndexSearchProxy(this);
    searchProxy_->setSourceModel(catalog_);

    installedProxy_ = new QSortFilterProxyModel(this);
    installedProxy_->setSourceModel(catalog_);
    installedProxy_->setFilterRole(IndexCatalog::StatusRole);
    installedProxy_->setFilterRegularExpression(QRegularExpression("^[12]$"));

    pages_ = new QStackedWidget(this);
    pages_->addWidget(buildDetectPage());     // 0
    pages_->addWidget(buildSettingsPage());   // 1
    pages_->addWidget(buildFaissPage());      // 2  <- index download page
    setCentralWidget(pages_);
    statusBar();                              // used for catalog error messages



    // ── vertical nav rail ──────────────────────────────────────────────────
    QToolBar* sideBar = new QToolBar("SideBar", this);
    sideBar->setObjectName("SideBar");        // matches the stylesheet in main.cpp
    addToolBar(Qt::LeftToolBarArea, sideBar);
    sideBar->setOrientation(Qt::Vertical);
    sideBar->setMovable(false);
    sideBar->setFloatable(false);
    sideBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    sideBar->setIconSize(QSize(26, 26));

    QStyle* st = QApplication::style();
    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    auto addPage = [&](const QIcon& icon, const QString& text, int page) {
        QAction* a = new QAction(icon, text, this);
        a->setCheckable(true);
        group->addAction(a);
        sideBar->addAction(a);
        connect(a, &QAction::triggered, this, [this, page] { pages_->setCurrentIndex(page); });
        return a;
    };

    QAction* aDetect = addPage(st->standardIcon(QStyle::SP_ComputerIcon),           "Detection",   0);
                       addPage(st->standardIcon(QStyle::SP_DriveNetIcon),           "Indexes",     2);
                       addPage(st->standardIcon(QStyle::SP_FileDialogDetailedView), "Settings",    1);
    aDetect->setChecked(true);

    setWindowTitle("TCG Deck Builder");
    resize(1440, 900);
}

// ── FAISS INDEX DOWNLOAD PAGE (QML) ─────────────────────────────────────────
QWidget* MainWindow::buildFaissPage() {
    auto* qw = new QQuickWidget;
    qw->rootContext()->setContextProperty("catalog", catalog_);
    qw->rootContext()->setContextProperty("indexList", searchProxy_);
    qw->rootContext()->setContextProperty("config", &Config::instance());
    qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qw->setSource(QUrl("qrc:/qml/DownloadPage.qml"));

    // Silence notif when loading default index
    connect(catalog_, &QAbstractListModel::modelReset, this, [this] {
        if (!Config::instance().getCurIndexId().isEmpty())
            indexNotifSilent_ = true;
    });

    // "Use" on a downloaded index -> point the app at it and (re)load the model
    connect(catalog_, &IndexCatalog::useIndexRequested, this, [this](const QString& dir) {
        indexEdit_->setText(dir);
        if (!onnxEdit_->text().isEmpty()) loadModel();     // hot-swap if a model is set
        else if(!indexNotifSilent_) QMessageBox::information(this, "Index selected",
                 "Index set. Choose the ONNX model in Settings, then press Load.");

        indexNotifSilent_ = false;

        // update default index
        QString id = dir.split("/").last();
        Config::instance().setCurIndexId(id);
    });
    connect(catalog_, &IndexCatalog::errorOccurred, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 8000);
    });
    return qw;
}

// ── SETTINGS (kept as Widgets: native file dialogs, simple forms) ───────────
QWidget* MainWindow::buildSettingsPage() {
    auto* w = new QWidget;
    auto* form = new QFormLayout(w);

    auto browseRow = [&](QLineEdit*& edit, const QString& label, bool dir) {
        edit = new QLineEdit;
        auto* btn = new QPushButton("Browse…");
        auto* row = new QHBoxLayout; row->addWidget(edit); row->addWidget(btn);
        form->addRow(label, row);
        connect(btn, &QPushButton::clicked, this, [this, edit, dir] {
            QString p = dir ? QFileDialog::getExistingDirectory(this, "Select folder")
                            : QFileDialog::getOpenFileName(this, "Select file");
            if (!p.isEmpty()) edit->setText(p);
        });
    };
    browseRow(onnxEdit_,    "ONNX model (.onnx):", false);
    browseRow(indexEdit_,   "FAISS index folder:", true);
    browseRow(mastersEdit_, "Masters folder:",     true);

    imgSizeSpin_ = new QSpinBox;
    imgSizeSpin_->setRange(64, 1024); imgSizeSpin_->setSingleStep(16); imgSizeSpin_->setValue(336);
    form->addRow("Image size:", imgSizeSpin_);

    nativeCheck_ = new QCheckBox("native aspect (letterbox + mask) — MUST match training/index");
    nativeCheck_->setChecked(true);
    form->addRow("", nativeCheck_);

    auto* loadBtn = new QPushButton("Load model + index");
    form->addRow("", loadBtn);
    connect(loadBtn, &QPushButton::clicked, this, &MainWindow::loadModel);

    modelStatus_ = new QLabel("No model loaded.");
    modelStatus_->setWordWrap(true);
    form->addRow("Status:", modelStatus_);
    return w;
}

void MainWindow::loadModel() {
    try {
        retriever_ = std::make_unique<TCGRetriever>(
            onnxEdit_->text().toStdString(),
            indexEdit_->text().toStdString(),
            mastersEdit_->text().toStdString(),
            nativeCheck_->isChecked(),
            imgSizeSpin_->value());
        modelStatus_->setText("Model + index loaded OK. Go to Detection.");
    } catch (const std::exception& e) {
        retriever_.reset();
        modelStatus_->setText(QString("FAILED: %1").arg(e.what()));
        QMessageBox::critical(this, "Load failed", e.what());
    }
    pushStateToQml();
}

// ── DETECTION: Widgets canvas on the left, QML panel on the right ──────────
QWidget* MainWindow::buildDetectPage() {
    auto* w = new QWidget;
    auto* outer = new QVBoxLayout(w);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(8, 8, 8, 0);
    auto* openBtn = new QPushButton("Open image");
    auto* rectBtn = new QPushButton("Rectangle");
    auto* polyBtn = new QPushButton("Polygon");
    auto* undoBtn = new QPushButton("Undo (Ctrl+Z)");
    auto* clrBtn  = new QPushButton("Clear");
    auto* detBtn  = new QPushButton("Detect");
    rectBtn->setCheckable(true); polyBtn->setCheckable(true); rectBtn->setChecked(true);
    for (auto* b : {openBtn, rectBtn, polyBtn, undoBtn, clrBtn, detBtn}) bar->addWidget(b);
    bar->addStretch();
    outer->addLayout(bar);

    auto* split = new QSplitter(Qt::Horizontal);
    canvas_ = new ImageCanvas;
    split->addWidget(canvas_);

    // ── QML panel ──────────────────────────────────────────────────────────
    candModel_    = new CandidateModel(this);
    selModel_     = new SelectionModel(this);
    bridge_       = new UiBridge(this);
    cropProvider_ = new CropImageProvider;      // engine takes ownership below
    db_ = new CardDatabase(this); // For global cards json

    candModel_->setCardDatabase(db_);

    //load cards_globa.json
    db_->load(QUrl("https://huggingface.co/datasets/SamTheCoder777/ws-index/resolve/main/cards_global.json"));

    qmlPanel_ = new QQuickWidget;
    qmlPanel_->engine()->addImageProvider("crop", cropProvider_);
    qmlPanel_->rootContext()->setContextProperty("bridge",   bridge_);
    qmlPanel_->rootContext()->setContextProperty("candModel", candModel_);
    qmlPanel_->rootContext()->setContextProperty("cardDatabase", db_);
    qmlPanel_->rootContext()->setContextProperty("selModel",  selModel_);
    qmlPanel_->rootContext()->setContextProperty("catalog",          catalog_);
    qmlPanel_->rootContext()->setContextProperty("installedIndexes", installedProxy_);
    qmlPanel_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qmlPanel_->setSource(QUrl("qrc:/qml/ResultsPanel.qml"));
    qmlPanel_->setMinimumWidth(470);
    split->addWidget(qmlPanel_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    outer->addWidget(split, 1);

    // toolbar wiring
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openImage);
    connect(rectBtn, &QPushButton::clicked, this, [this, rectBtn, polyBtn] {
        canvas_->setMode(ImageCanvas::Rectangle);
        rectBtn->setChecked(true); polyBtn->setChecked(false);
    });
    connect(polyBtn, &QPushButton::clicked, this, [this, rectBtn, polyBtn] {
        canvas_->setMode(ImageCanvas::Polygon);
        polyBtn->setChecked(true); rectBtn->setChecked(false);
    });
    connect(undoBtn, &QPushButton::clicked, this, [this] { canvas_->undo(); });
    connect(clrBtn,  &QPushButton::clicked, this, [this] { canvas_->clearSelections(); });
    connect(detBtn,  &QPushButton::clicked, this, &MainWindow::runDetection);

    // canvas -> app
    connect(canvas_, &ImageCanvas::selectionsChanged, this, [this] {
        syncSelections(); pushStateToQml();
    });
    connect(canvas_, &ImageCanvas::selectionClicked, this, &MainWindow::showSelectionResults);
    connect(canvas_, &ImageCanvas::selectionGeometryChanged, this, [this](int i) {
        if (i >= 0 && i < sel_.size()) {
            sel_[i].cands.clear();
            sel_[i].confirmed = false;
            sel_[i].cardId.clear();
            canvas_->setSelectionState(i, false, QString());
            if (i == currentSel_) showSelectionResults(i); else pushStateToQml();
        }
    });

    // QML -> app
    connect(bridge_, &UiBridge::selectCardRequested, this, &MainWindow::showSelectionResults);
    connect(bridge_, &UiBridge::confirmRequested,    this, &MainWindow::confirmCandidate);
    connect(bridge_, &UiBridge::exportRequested,     this, &MainWindow::exportDeck);
    connect(bridge_, &UiBridge::detectRequested,     this, &MainWindow::runDetection);
    connect(bridge_, &UiBridge::quantityRequested,   this, [this](int q) {
        if (currentSel_ >= 0 && currentSel_ < sel_.size()) {
            sel_[currentSel_].qty = q;
            canvas_->setSelectionState(currentSel_, sel_[currentSel_].confirmed,
                QString("%1 x%2").arg(toDeckCode(sel_[currentSel_].cardId)).arg(q));
            pushStateToQml();
        }
    });

    return w;
}

void MainWindow::syncSelections() {
    int n = canvas_->selectionCount();
    while (sel_.size() > n) sel_.removeLast();
    while (sel_.size() < n) sel_.push_back(SelState{});
    if (currentSel_ >= n) currentSel_ = -1;
}

// push list + summary + per-card state into the QML models/bridge
void MainWindow::pushStateToQml() {
    QVector<SelectionModel::Row> rows;
    int confirmed = 0, total = 0;
    for (const auto& s : sel_) {
        SelectionModel::Row r;
        r.confirmed = s.confirmed;
        r.qty       = s.qty;
        r.label     = s.confirmed
                        ? QString("%1  x%2").arg(toDeckCode(s.cardId)).arg(s.qty)
                        : QStringLiteral("(not confirmed)");
        rows.push_back(r);
        if (s.confirmed) { ++confirmed; total += s.qty; }
    }
    selModel_->setRows(rows);

    QString summary = QString("%1 selected · %2 confirmed · %3 cards")
                        .arg(sel_.size()).arg(confirmed).arg(total);
    bool isConf = (currentSel_ >= 0 && currentSel_ < sel_.size() && sel_[currentSel_].confirmed);
    QString confText = isConf ? QString("Confirmed: %1").arg(toDeckCode(sel_[currentSel_].cardId))
                              : QStringLiteral("Not confirmed");
    int qty = (currentSel_ >= 0 && currentSel_ < sel_.size()) ? sel_[currentSel_].qty : 1;
    bridge_->setState(summary, confText, isConf, qty, currentSel_, retriever_ != nullptr);
}

void MainWindow::openImage() {
    QString p = QFileDialog::getOpenFileName(this, "Open image", {},
                    "Images (*.png *.jpg *.jpeg *.webp *.bmp)");
    if (p.isEmpty()) return;
    sourceBgr_ = cv::imread(p.toStdString(), cv::IMREAD_COLOR);
    if (sourceBgr_.empty()) { QMessageBox::warning(this, "Error", "Could not read image."); return; }
    sourcePath_ = p;
    canvas_->setImage(matToQImage(sourceBgr_));
    sel_.clear();
    currentSel_ = -1;
    candModel_->clear();
    pushStateToQml();
}

// 4 points -> perspective warp (deskew, zero background); 5+ -> polygon mask.
cv::Mat MainWindow::cropForSelection(int index) const {
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
    for (const QPointF& p : poly)
        pts.emplace_back((int)std::lround(p.x() - x0), (int)std::lround(p.y() - y0));
    cv::Mat mask(crop.size(), CV_8UC1, cv::Scalar(0));
    std::vector<std::vector<cv::Point>> polys{pts};
    cv::fillPoly(mask, polys, cv::Scalar(255));
    cv::Mat out(crop.size(), crop.type(), cv::Scalar(114, 114, 114));
    crop.copyTo(out, mask);
    return out;
}

void MainWindow::runDetection() {
    if (!retriever_) { QMessageBox::information(this, "No model", "Load a model in Settings first."); return; }
    syncSelections();
    if (sel_.isEmpty()) { QMessageBox::information(this, "No selection", "Select at least one card."); return; }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (int i = 0; i < sel_.size(); ++i) {
        if (sel_[i].confirmed) continue;
        cv::Mat crop = cropForSelection(i);
        if (!crop.empty()) sel_[i].cands = retriever_->search(crop, 15);
    }
    QApplication::restoreOverrideCursor();
    showSelectionResults(0);
}

void MainWindow::showSelectionResults(int index) {
    if (index < 0 || index >= sel_.size()) return;
    currentSel_ = index;
    canvas_->setHighlight(index);

    cv::Mat crop = cropForSelection(index);
    cropProvider_->setImage(crop.empty() ? QImage() : matToQImage(crop));
    bridge_->bumpCrop();                       // busts the QML image cache

    candModel_->setCandidates(sel_[index].cands, sel_[index].cardId);
    pushStateToQml();
}

void MainWindow::confirmCandidate(int candIndex) {
    if (currentSel_ < 0 || currentSel_ >= sel_.size()) return;
    auto& s = sel_[currentSel_];
    if (candIndex < 0 || candIndex >= (int)s.cands.size()) return;

    s.confirmed = true;
    s.cardId    = s.cands[candIndex].card_id;

    canvas_->setSelectionState(currentSel_, true,
        QString("%1 x%2").arg(toDeckCode(s.cardId)).arg(s.qty));
    candModel_->setConfirmedId(s.cardId);
    pushStateToQml();
}

void MainWindow::exportDeck() {
    QStringList lines;
    int missing = 0;
    for (const auto& s : sel_) {
        if (!s.confirmed) { ++missing; continue; }
        for (int k = 0; k < s.qty; ++k) lines << toDeckCode(s.cardId);
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