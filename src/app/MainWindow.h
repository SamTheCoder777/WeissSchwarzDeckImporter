// MainWindow.h — Widgets shell (canvas + toolbar) with a QML results panel.
#pragma once

#include <QFutureWatcher>
#include <QLabel>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSqlDatabase>
#include <QVector>
#include <functional>
#include <memory>
#include <opencv2/core.hpp>

class DatabaseUtil;
class DatasetManager;
class IndexSearchProxy;
class IndexGenerationPage;
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

QString toDeckCode(const std::string &cardId); // defined in Models.cpp

// For onetime model setup
struct ModelRow
{
    QProgressBar *bar;
    QLabel *status;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildSidebar();
    void updateSettingsDot();
    void checkFirstLaunch();
    void showModelSetupDialog(QWidget *parent, QBitArray modelNotDownloaded);
    void downloadModel(ModelRow idRow,
                       ModelRow detRow,
                       QDialog *dlg,
                       QBitArray modelNotDownloaded,
                       std::function<void()> onFail,
                       std::function<void()> onDone);
    void downloadFile(const QUrl &url,
                      const QString &destPath,
                      ModelRow row,
                      QDialog *dlg,
                      std::shared_ptr<bool> failed,
                      std::function<void()> onFail,
                      std::function<void()> onSuccess);

    // shared services (owned here)
    SeriesRepository *seriesRepo_ = nullptr;
    IndexCatalog *catalog_ = nullptr;
    SeriesCatalog *seriesCatalog_ = nullptr;
    IndexSearchProxy *searchProxy_ = nullptr;
    QSortFilterProxyModel *installedProxy_ = nullptr;
    DatasetManager *cardListDbManager_ = nullptr;
    DatasetManager *seriesListDbManager_ = nullptr;
    DatabaseUtil *dbUtil_ = nullptr;
    ModelService *models_ = nullptr;
    SelectionModel *selModel_ = nullptr; // shared: detection writes, gallery reads
    UiBridge *bridge_ = nullptr;         // shared

    // pages
    QStackedWidget *pages_ = nullptr;
    DetectionPage *detection_ = nullptr;
    SettingsPage *settings_ = nullptr;
    FaissPage *faiss_ = nullptr;
    GalleryPage *gallery_ = nullptr;
    CardsPage *cards_ = nullptr;
    IndexGenerationPage *indexGen_ = nullptr;

    QAction *settingsAction_ = nullptr;
    bool seriesUpdateAvailable_ = false;
    bool appUpdateAvailable_ = false;
    bool indexNotifSilent_ = false;

    QNetworkAccessManager nam_;
};
