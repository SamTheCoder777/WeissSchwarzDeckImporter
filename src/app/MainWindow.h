// MainWindow.h — Widgets shell (canvas + toolbar) with a QML results panel.
#pragma once

#include <QFutureWatcher>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QSqlDatabase>
#include <QVector>
#include <memory>
#include <opencv2/core.hpp>
#include "../database/DatabaseUtil.h"
#include "../database/DatasetManager.h"
#include "../retrieval/tcg_infer.h"
#include "../core/Config.h"
#include "../index/IndexSearchProxy.h"
#include "../retrieval/CardDetector.h"
#include "../services/ModelService.h"
#include "../detection/DetectionPage.h"
#include "../index/FaissPage.h"
#include "../gallery/GalleryPage.h"
#include "../settings/SettingsPage.h"
#include "../index/FaissPage.h"
#include "../cardsIndex/SeriesCatalog.h"
#include "../cardsIndex/CardsPage.h"
#include "../cardsIndex/SeriesRepository.h"

class SeriesRepository;
class SeriesCatalog;
class IndexCatalog;
class SelectionModel;
class UiBridge;
class ModelService;
class DetectionPage;
class SettingsPage;
class FaissPage;
class CardsPage;
class GalleryPage;
class QStackedWidget;
class QSortFilterProxyModel;

QString toDeckCode(const std::string& cardId);   // defined in Models.cpp

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildSidebar();

    // shared services (owned here)
    SeriesRepository*      seriesRepo_     = nullptr;
    IndexCatalog*          catalog_        = nullptr;
    SeriesCatalog*         seriesCatalog_  = nullptr;
    IndexSearchProxy*      searchProxy_    = nullptr;
    QSortFilterProxyModel* installedProxy_ = nullptr;
    DatasetManager*   cardListDbManager_   = nullptr;
    DatasetManager*   seriesListDbManager_ = nullptr;
    DatabaseUtil*          dbUtil_         = nullptr;
    ModelService*          models_         = nullptr;
    SelectionModel*        selModel_       = nullptr;   // shared: detection writes, gallery reads
    UiBridge*              bridge_         = nullptr;   // shared

    // pages
    QStackedWidget* pages_    = nullptr;
    DetectionPage*  detection_= nullptr;
    SettingsPage*   settings_ = nullptr;
    FaissPage*      faiss_    = nullptr;
    GalleryPage*    gallery_  = nullptr;
    CardsPage*      cards_    = nullptr;

    bool indexNotifSilent_ = false;
};
