#include "GalleryPage.h"

#include "../images/CardImageProvider.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QVBoxLayout>

GalleryPage::GalleryPage(DatabaseUtil *dbUtil, SelectionModel *selModel, UiBridge *bridge, QWidget *parent):
    dbUtil_(dbUtil), selModel_(selModel), bridge_(bridge), QWidget(parent)
{
    buildUi();
}

void GalleryPage::buildUi()
{
    auto* qw = new QQuickWidget(this);
    qw->rootContext()->setContextProperty("cardDatabase", dbUtil_);
    qw->rootContext()->setContextProperty("selModel", selModel_);
    qw->rootContext()->setContextProperty("bridge", bridge_);
    qw->engine()->addImageProvider("cardcache", new CardImageProvider(dbUtil_));
    qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qw->setSource(QUrl("qrc:/qml/gallery/GalleryPage.qml"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(qw);
}
