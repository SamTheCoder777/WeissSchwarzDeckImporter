#include "CardsPage.h"
#include "../core/Config.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QVBoxLayout>
#include <QMessageBox>

CardsPage::CardsPage(SeriesCatalog* catalog, QWidget* parent)
    : QWidget(parent), catalog_(catalog) {
    searchProxy_ = new SeriesSearchProxy(this);
    searchProxy_->setSourceModel(catalog_);
    buildUi();
}

void CardsPage::buildUi() {
    auto* qw = new QQuickWidget(this);
    qw->rootContext()->setContextProperty("catalog", catalog_);
    qw->rootContext()->setContextProperty("seriesList", searchProxy_);
    qw->rootContext()->setContextProperty("config", &Config::instance());
    qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qw->setSource(QUrl("qrc:/qml/download/CardsDownloadPage.qml"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(qw);

    connect(catalog_, &SeriesCatalog::errorOccurred, this, [this](const QString& msg) {
        QMessageBox::critical(this, "Series / Cards Error", msg);
    });
}