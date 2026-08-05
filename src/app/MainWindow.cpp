#include "MainWindow.h"

#include "../viewmodels/Models.h"
#include "../viewmodels/UiBridge.h"
#include "../index/IndexCatalog.h"

#include <QtWidgets>
#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSortFilterProxyModel>
#include <QRegularExpression>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QShortcut>
#include <QSqlDatabase>

static QImage matToQImage(const cv::Mat& bgr) {
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, (int)rgb.step,
                  QImage::Format_RGB888).copy();
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {

    catalog_ = new IndexCatalog(this);

    searchProxy_ = new IndexSearchProxy(this);
    searchProxy_->setSourceModel(catalog_);

    installedProxy_ = new QSortFilterProxyModel(this);
    installedProxy_->setSourceModel(catalog_);
    installedProxy_->setFilterRole(IndexCatalog::StatusRole);
    installedProxy_->setFilterRegularExpression(QRegularExpression("^[12]$"));

    dbManager_ = new DatasetManager(QUrl(Config::instance().getDatasetSourceUrl()), this);
    dbUtil_    = new DatabaseUtil(this);
    models_    = new ModelService(this);

    // shared view-models (used by detection + gallery)
    selModel_ = new SelectionModel(this);
    bridge_   = new UiBridge(this);

    // ── pages ───────────────────────────────────────────────────────────────
    detection_ = new DetectionPage(models_, dbUtil_, dbManager_, selModel_, bridge_,
                                   catalog_, installedProxy_, this);
    settings_  = new SettingsPage(models_, dbManager_, this);
    faiss_     = new FaissPage(models_, catalog_, this);
    gallery_   = new GalleryPage(dbUtil_, selModel_, bridge_, this);

    pages_ = new QStackedWidget(this);
    pages_->addWidget(detection_);   // 0
    pages_->addWidget(settings_);    // 1
    pages_->addWidget(faiss_);       // 2
    pages_->addWidget(gallery_);     // 3
    setCentralWidget(pages_);
    statusBar();

    buildSidebar();
    setWindowTitle("TCG Deck Builder");
    resize(1440, 900);
}

void MainWindow::buildSidebar() {
    QToolBar* sideBar = new QToolBar("SideBar", this);
    sideBar->setObjectName("SideBar");
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

    QAction* aDetect = addPage(st->standardIcon(QStyle::SP_ComputerIcon),           "Detection", 0);
    addPage(st->standardIcon(QStyle::SP_DriveNetIcon),           "Indexes",   2);
    addPage(st->standardIcon(QStyle::SP_FileDialogDetailedView), "Settings",  1);
    addPage(st->standardIcon(QStyle::SP_DriveCDIcon),            "Gallery",   3);
    aDetect->setChecked(true);
}


    /**
     // Faiss catalog
    catalog_ = new IndexCatalog(this);

    searchProxy_ = new IndexSearchProxy(this);
    searchProxy_->setSourceModel(catalog_);

    installedProxy_ = new QSortFilterProxyModel(this);
    installedProxy_->setSourceModel(catalog_);
    installedProxy_->setFilterRole(IndexCatalog::StatusRole);
    installedProxy_->setFilterRegularExpression(QRegularExpression("^[12]$"));

    // get global cards database
    QUrl datasetUrl(Config::instance().getDatasetSourceUrl());
    dbManager_ = new DatasetManager(datasetUrl, this);
    dbUtil_ = new DatabaseUtil(this);

    pages_ = new QStackedWidget(this);
    pages_->addWidget(buildDetectPage());     // 0 (Call first since it inits various models)
    pages_->addWidget(buildSettingsPage());   // 1
    pages_->addWidget(buildFaissPage());      // 2  <- index download page
    pages_->addWidget(buildGalleryPage());    // 3
    setCentralWidget(pages_);
    statusBar();                              // used for catalog error messages

    candModel_->setDatabaseUtil(dbUtil_);

    auto refreshModel = [this]() {
        QSqlDatabase db = dbManager_->getUiDatabase();
        if (db.isOpen()) {
            candModel_->setCardDatabase(db);
        } else {
            qWarning() << "Database unavailable; model not updated.";
        }
    };

    QElapsedTimer t; t.start();
    refreshModel();
    qDebug() << "refreshModel took" << t.elapsed() << "ms";


    connect(dbManager_, &DatasetManager::readyToUse, this, refreshModel);

    // check for update
    QElapsedTimer t2; t2.start();
    dbManager_->checkForUpdates();
    qDebug() << "checkForUpdates took" << t2.elapsed() << "ms";

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
                       addPage(st->standardIcon(QStyle::SP_DriveCDIcon), "Gallery",    3);
    aDetect->setChecked(true);

    setWindowTitle("TCG Deck Builder");
    resize(1440, 900);

    **/