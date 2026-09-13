#include "MainWindow.h"

#include "../cardsIndex/CardsPage.h"
#include "../cardsIndex/SeriesCatalog.h"
#include "../cardsIndex/SeriesRepository.h"
#include "../core/Config.h"
#include "../database/DatabaseUtil.h"
#include "../database/DatasetManager.h"
#include "../detection/DetectionPage.h"
#include "../gallery/GalleryPage.h"
#include "../index/FaissPage.h"
#include "../index/IndexCatalog.h"
#include "../index/IndexGenerationPage.h"
#include "../index/IndexSearchProxy.h"
#include "../models/tcg_infer.h"
#include "../retrieval/CardDetector.h"
#include "../services/ModelService.h"
#include "../settings/SettingsPage.h"
#include "../viewmodels/Models.h"
#include "../viewmodels/UiBridge.h"

#include <QFutureWatcher>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWidget>
#include <QRegularExpression>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QSqlDatabase>
#include <QtConcurrent>
#include <QtWidgets>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

static QImage matToQImage(const cv::Mat &bgr)
{
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, (int) rgb.step, QImage::Format_RGB888).copy();
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowIcon(QIcon(":/icon/logo.ico"));

    qDebug() << "Cache Location:"
             << QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    dbUtil_ = new DatabaseUtil(this);
    dbUtil_->setLocale(Config::instance().getPreferredLocale());
    dbUtil_->cleanupExpiredMissing();
    models_ = new ModelService(this);

    connect(models_, &ModelService::statusChanged, this, [this](QString msg) {
        statusBar()->showMessage(msg);
    });

    catalog_ = new IndexCatalog(this);
    seriesRepo_ = new SeriesRepository(this);
    seriesRepo_->checkForUpdates();
    seriesCatalog_ = new SeriesCatalog(seriesRepo_, this);

    connect(seriesRepo_,
            &SeriesRepository::updateAvailable,
            this,
            [this](SeriesRepository::UpdateStatus s) {
                seriesUpdateAvailable_ = (s == SeriesRepository::UpdateStatus::UpdateAvailable);
                updateSettingsDot();
            });

    connect(seriesRepo_, &SeriesRepository::seriesListUpdated, this, [this] {
        seriesUpdateAvailable_ = false;
        updateSettingsDot();
    });

    searchProxy_ = new IndexSearchProxy(this);
    searchProxy_->setSourceModel(catalog_);

    installedProxy_ = new QSortFilterProxyModel(this);
    installedProxy_->setSourceModel(catalog_);
    installedProxy_->setFilterRole(IndexCatalog::StatusRole);
    installedProxy_->setFilterRegularExpression(QRegularExpression("^[12]$"));

    selModel_ = new SelectionModel(this);
    bridge_ = new UiBridge(this);

    // pages
    detection_
        = new DetectionPage(models_, dbUtil_, selModel_, bridge_, catalog_, installedProxy_, this);
    settings_ = new SettingsPage(models_, seriesRepo_, catalog_, dbUtil_, this);
    faiss_ = new FaissPage(models_, catalog_, this);
    cards_ = new CardsPage(seriesCatalog_, this);
    gallery_ = new GalleryPage(dbUtil_, selModel_, bridge_, this);
    indexGen_ = new IndexGenerationPage(models_, this);

    pages_ = new QStackedWidget(this);
    pages_->addWidget(detection_);
    pages_->addWidget(settings_);
    pages_->addWidget(faiss_);
    pages_->addWidget(gallery_);
    pages_->addWidget(cards_);
    pages_->addWidget(indexGen_);
    setCentralWidget(pages_);
    statusBar();

    buildSidebar();
    updateSettingsDot();
    setWindowTitle("WS Deck Importer");
    resize(1440, 900);
}

void MainWindow::buildSidebar()
{
    QToolBar *sideBar = new QToolBar("SideBar", this);
    sideBar->setObjectName("SideBar");
    addToolBar(Qt::LeftToolBarArea, sideBar);
    sideBar->setOrientation(Qt::Vertical);
    sideBar->setMovable(false);
    sideBar->setFloatable(false);
    sideBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    sideBar->setIconSize(QSize(26, 26));
    sideBar->setStyleSheet("#SideBar QToolButton {"
                           "    width: 100%;"
                           "    padding: 6px 0px;"
                           "}");

    QStyle *st = QApplication::style();
    auto *group = new QActionGroup(this);
    group->setExclusive(true);

    auto addPage = [&](const QIcon &icon, const QString &text, int page) {
        QAction *a = new QAction(icon, text, this);
        a->setCheckable(true);
        a->setToolTip("");
        group->addAction(a);
        sideBar->addAction(a);
        connect(a, &QAction::triggered, this, [this, page] { pages_->setCurrentIndex(page); });
        return a;
    };

    QWidget *topSpacer = new QWidget(this);
    topSpacer->setMinimumHeight(15);
    sideBar->addWidget(topSpacer);

    QAction *aDetect = addPage(QIcon(":/icon/magnifying.svg"), "Detection", 0);
    addPage(QIcon(":/icon/gallery.svg"), "Selected Gallery", 3);
    sideBar->addWidget(topSpacer);
    sideBar->addSeparator();
    sideBar->addWidget(topSpacer);
    addPage(QIcon(":/icon/indexes_dl.svg"), "Faiss Indexes", 2);
    addPage(QIcon(":/icon/cards_dl.svg"), "Cards Indexes", 4);
    addPage(QIcon(":/icon/add_diamond.svg"), "Index Generator", 5);
    sideBar->addWidget(topSpacer);
    sideBar->addSeparator();
    sideBar->addWidget(topSpacer);
    settingsAction_ = addPage(QIcon(":/icon/settings.svg"), "Settings", 1);
    aDetect->setChecked(true);

    connect(settingsAction_, &QAction::triggered, this, [this] {
        seriesUpdateAvailable_ = false;
        updateSettingsDot();
    });
}

void MainWindow::updateSettingsDot()
{
    if (!settingsAction_)
        return;
    QToolBar *bar = findChild<QToolBar *>("SideBar");
    if (!bar)
        return;
    QWidget *btn = bar->widgetForAction(settingsAction_);
    if (!btn)
        return;

    QLabel *dot = btn->findChild<QLabel *>("updateDot");
    if (!dot) {
        dot = new QLabel(btn);
        dot->setObjectName("updateDot");
        dot->setFixedSize(9, 9);
        dot->setStyleSheet("background:#e5484d; border-radius:4px;");
        dot->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    dot->move(btn->width() - 14, 6);
    dot->setVisible(seriesUpdateAvailable_);
    dot->raise();
}