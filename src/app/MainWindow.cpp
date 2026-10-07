#include "MainWindow.h"

#include "../cardsIndex/CardsPage.h"
#include "../cardsIndex/SeriesCatalog.h"
#include "../cardsIndex/SeriesRepository.h"
#include "../core/Config.h"
#include "../database/DatabaseUtil.h"
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
            [this](Config::DetectLocaleMode region, SeriesRepository::UpdateStatus s) {
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

    QTimer::singleShot(0, this, &MainWindow::checkFirstLaunch);
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
        connect(a, &QAction::triggered, this, [this, page] {
            pages_->setCurrentIndex(page);

            // if settings clicked, update model paths
            if (page == 1) {
                settings_->updateModelPaths();
            }
        });
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
    addPage(QIcon(":/icon/cards_dl.svg"), "Card Indexes", 4);
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

    connect(settings_, &SettingsPage::appUpdateStateChanged, this, [this](bool available) {
        appUpdateAvailable_ = available;
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
    dot->setVisible(seriesUpdateAvailable_ || appUpdateAvailable_);
    dot->raise();
}

void MainWindow::checkFirstLaunch()
{
    QBitArray modelNotDownloaded(2);

    modelNotDownloaded[0] = Config::instance().getCurModelPath().isEmpty();
    modelNotDownloaded[1] = Config::instance().getCurYoloModelPath().isEmpty();

    if (modelNotDownloaded.count(true) > 0) {
        showModelSetupDialog(this, modelNotDownloaded);
    }
}

static void setStatus(QLabel *l, const QString &text, const char *state)
{
    if (!l)
        return;
    l->setText(text);
    l->setProperty("state", state);
    l->style()->unpolish(l);
    l->style()->polish(l);
}

static ModelRow addModelCard(QVBoxLayout *parent,
                             const QString &title,
                             const QString &desc,
                             bool missing)
{
    auto *card = new QFrame;
    card->setObjectName("sectionCard");
    auto *grid = new QGridLayout(card);
    grid->setContentsMargins(18, 16, 18, 16);
    grid->setVerticalSpacing(10);

    auto *t = new QLabel(title);
    t->setObjectName("cardTitle");

    auto *d = new QLabel(desc);
    d->setObjectName("modelDesc");
    d->setWordWrap(true);

    auto *s = new QLabel;
    s->setObjectName("status");
    if (missing)
        setStatus(s, "Not downloaded", "missing");
    else
        setStatus(s, "Ready", "ready");

    auto *bar = new QProgressBar;
    bar->setRange(0, 100);
    bar->setTextVisible(false);
    bar->hide();

    grid->addWidget(t, 0, 0);
    grid->addWidget(s, 0, 1, Qt::AlignRight);
    grid->addWidget(d, 1, 0, 1, 2);
    grid->addWidget(bar, 2, 0, 1, 2);
    parent->addWidget(card);
    return {bar, s};
}

void MainWindow::showModelSetupDialog(QWidget *parent, QBitArray modelNotDownloaded)
{
    QDialog dlg(parent);
    dlg.setWindowTitle("Set up models");
    dlg.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dlg.setMinimumSize(560, 460);
    dlg.resize(620, 500);

    auto *v = new QVBoxLayout(&dlg);
    v->setContentsMargins(32, 28, 32, 24);
    v->setSpacing(14);

    auto *heading = new QLabel("Welcome");
    heading->setObjectName("pageTitle");

    auto *sub = new QLabel("To identify cards, the app needs two recognition models. "
                           "Download them once and you're ready to go.");
    sub->setObjectName("setupSub");
    sub->setWordWrap(true);

    auto *hint = new QLabel("Also follow this "
                            "<a "
                            "href=\"https://github.com/SamTheCoder777/WeissSchwarzDeckImporter/"
                            "blob/main/QUICKSTART.md\" style=\"color:#4aa3ff; "
                            "text-decoration:none;\">quick guide</a> "
                            "to learn how to use the app");
    hint->setObjectName("setupHint");
    hint->setTextFormat(Qt::RichText);
    hint->setOpenExternalLinks(true);
    hint->setWordWrap(true);
    hint->setTextInteractionFlags(Qt::TextBrowserInteraction);

    auto *doneLabel = new QLabel(
        "Done setting up the models! Now you are ready to start detecting decks.<br><br><br>"
        "Go to this "
        "<a "
        "href=\"https://github.com/SamTheCoder777/WeissSchwarzDeckImporter/blob/main/"
        "QUICKSTART.md\" style=\"color:#4aa3ff; "
        "text-decoration:none;\">quick start page</a> "
        "to learn how to use the app.");
    doneLabel->setObjectName("setupSub");
    doneLabel->setTextFormat(Qt::RichText);
    doneLabel->setOpenExternalLinks(true);
    doneLabel->setWordWrap(true);
    doneLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    doneLabel->hide();

    v->addWidget(heading);
    v->addWidget(sub);
    v->addWidget(hint);
    v->addSpacing(8);

    auto *cardsBox = new QWidget;
    auto *cardsLayout = new QVBoxLayout(cardsBox);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    cardsLayout->setSpacing(14);

    ModelRow idRow = addModelCard(cardsLayout,
                                  "Card identifier",
                                  "Matches a card image to the right card.",
                                  modelNotDownloaded[0]);
    ModelRow detRow = addModelCard(cardsLayout,
                                   "Card detector",
                                   "Finds card locations in your photos.",
                                   modelNotDownloaded[1]);
    v->addWidget(cardsBox);
    v->addStretch();
    v->addWidget(doneLabel);
    v->addStretch();

    auto *buttons = new QHBoxLayout;
    auto *later = new QPushButton("Later");
    later->setObjectName("actionGhost");
    auto *btn = new QPushButton("Download && set up");
    btn->setObjectName("actionAccent");
    btn->setDefault(true);
    buttons->addStretch();
    buttons->addWidget(later);
    buttons->addWidget(btn);
    v->addLayout(buttons);

    bool done = false;

    auto onDone = [&] {
        done = true;
        heading->hide();
        sub->hide();
        hint->hide();
        cardsBox->hide();
        later->hide();

        doneLabel->show();
        btn->setText("Start detecting");
        btn->setEnabled(true);

        dlg.setMinimumSize(480, 260);
        dlg.resize(520, 280);
    };

    connect(later, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btn, &QPushButton::clicked, btn, [&, btn] {
        if (done) {
            dlg.accept();
            return;
        }
        btn->setEnabled(false);
        btn->setText("Downloading…");
        auto onFail = [guard = QPointer<QPushButton>(btn)] {
            if (guard) {
                guard->setEnabled(true);
                guard->setText("Retry");
            }
        };
        downloadModel(idRow, detRow, &dlg, modelNotDownloaded, onFail, onDone);
    });

    dlg.exec();
}

void MainWindow::downloadModel(ModelRow idRow,
                               ModelRow detRow,
                               QDialog *dlg,
                               QBitArray modelNotDownloaded,
                               std::function<void()> onFail,
                               std::function<void()> onDone)
{
    const QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + "/models";
    QDir().mkpath(destDir);
    const QString destIdPath = destDir + "/card_identifier.onnx";
    const QString destDetPath = destDir + "/card_detector.onnx";

    auto pending = std::make_shared<int>(0);
    auto failed = std::make_shared<bool>(false);
    QPointer<QDialog> dlgGuard(dlg);

    auto finishIfDone = [this, dlgGuard, pending, failed, onDone] {
        if (*pending == 0 && !*failed) {
            QTimer::singleShot(0, this, [this]() { models_->load(true); });
            if (dlgGuard)
                onDone();
        }
    };
    auto onOneDone = [pending, finishIfDone] {
        --*pending;
        finishIfDone();
    };

    if (modelNotDownloaded[0]) {
        if (QFile::exists(destIdPath)) {
            Config::instance().setCurModelPath(destIdPath);
            setStatus(idRow.status, "Ready", "ready");
        } else {
            ++*pending;
            downloadFile(Config::instance().CardIdentifierDl_,
                         destIdPath,
                         idRow,
                         dlg,
                         failed,
                         onFail,
                         [=, this] {
                             Config::instance().setCurModelPath(destIdPath);
                             onOneDone();
                         });
        }
    }

    if (modelNotDownloaded[1]) {
        if (QFile::exists(destDetPath)) {
            Config::instance().setCurYoloModelPath(destDetPath);
            setStatus(detRow.status, "Ready", "ready");
        } else {
            ++*pending;
            downloadFile(Config::instance().CardDetectorDl_,
                         destDetPath,
                         detRow,
                         dlg,
                         failed,
                         onFail,
                         [=, this] {
                             Config::instance().setCurYoloModelPath(destDetPath);
                             onOneDone();
                         });
        }
    }

    finishIfDone();
}

void MainWindow::downloadFile(const QUrl &url,
                              const QString &destPath,
                              ModelRow row,
                              QDialog *dlg,
                              std::shared_ptr<bool> failed,
                              std::function<void()> onFail,
                              std::function<void()> onSuccess)
{
    QPointer<QProgressBar> bar(row.bar);
    QPointer<QLabel> status(row.status);
    QPointer<QDialog> dlgGuard(dlg);

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "TCGDeckBuilder/1.0");

    const QString partPath = destPath + ".part";
    auto *file = new QFile(partPath);
    if (!file->open(QIODevice::WriteOnly)) {
        QMessageBox::warning(dlg,
                             "Download failed",
                             "Cannot write to " + partPath + ": " + file->errorString());
        delete file;
        *failed = true;
        setStatus(status, "Failed", "error");
        onFail();
        return;
    }

    bar->setValue(0);
    bar->show();
    setStatus(status, "Downloading…", "downloading");

    QNetworkReply *reply = nam_.get(req);
    file->setParent(reply);
    connect(dlg, &QDialog::finished, reply, &QNetworkReply::abort);

    connect(reply, &QNetworkReply::downloadProgress, reply, [bar, status](qint64 got, qint64 total) {
        if (total > 0 && bar) {
            const int pct = int(100.0 * got / total);
            bar->setValue(pct);
            if (status)
                status->setText(QString("Downloading… %1%").arg(pct));
        }
    });
    connect(reply, &QNetworkReply::readyRead, reply, [reply, file] {
        file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [=, this] {
        file->write(reply->readAll());
        file->close();

        const auto err = reply->error();
        const QString errStr = reply->errorString();
        reply->deleteLater();

        QWidget *msgParent = dlgGuard ? static_cast<QWidget *>(dlgGuard.data()) : this;

        if (err != QNetworkReply::NoError) {
            QFile::remove(partPath);
            if (err == QNetworkReply::OperationCanceledError)
                return;
            setStatus(status, "Failed", "error");
            if (!*failed)
                QMessageBox::warning(msgParent, "Download failed", errStr);
            *failed = true;
            onFail();
            return;
        }

        QFile::remove(destPath);
        if (!QFile::rename(partPath, destPath)) {
            setStatus(status, "Failed", "error");
            QMessageBox::warning(msgParent, "Download failed", "Could not finalize " + destPath);
            *failed = true;
            onFail();
            return;
        }
        setStatus(status, "Ready", "ready");
        if (bar)
            bar->setValue(100);
        onSuccess();
    });
}