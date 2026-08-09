#include "FaissPage.h"

#include "../core/Config.h"

#include <QMessageBox>
#include <QQmlContext>
#include <QQmlEngine>
#include <QVBoxLayout>

FaissPage::FaissPage(ModelService *models, IndexCatalog *catalog, QWidget *parent): QWidget(parent),
    catalog_(catalog), models_(models) {
    searchProxy_ = new IndexSearchProxy(this);
    searchProxy_->setSourceModel(catalog_);

    buildUi();

}

void FaissPage::buildUi()
{
    auto* qw = new QQuickWidget(this);
    qw->rootContext()->setContextProperty("catalog", catalog_);
    qw->rootContext()->setContextProperty("indexList", searchProxy_);
    qw->rootContext()->setContextProperty("config", &Config::instance());
    qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qw->setSource(QUrl("qrc:/qml/DownloadPage.qml"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(qw);

    // Silence notif when loading default index
    connect(catalog_, &QAbstractListModel::modelReset, this, [this] {
        if (!Config::instance().getCurIndexId().isEmpty())
            models_->setSilent(true);
    });

    // "Use" on a downloaded index -> point the app at it and (re)load the model
    connect(catalog_, &IndexCatalog::useIndexRequested, this, [this](const QString& dir) {
        models_->setIndexDir(dir);
        // update default index
        QString id = dir.split("/").last();
        Config::instance().setCurIndexId(id);

        if (models_->isLoaded()) {
            try{
                models_->load();     // hot-swap if a model is set
            }catch(const std::exception& e){
                QMessageBox::critical(this, "Error Loading Model", QString::fromStdString(e.what()));
            }
        }
        else if(!models_->isSilent()) QMessageBox::information(this, "Index selected",
                                     "Index set. Choose the ONNX model in Settings, then press Load.");

        models_->setSilent(false);
    });
    connect(catalog_, &IndexCatalog::errorOccurred, this, [this](const QString& msg) {
        QMessageBox::critical(this, "Index Catalog Error", msg);
    });
}
