#include "GalleryPage.h"

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
    qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qw->setSource(QUrl("qrc:/qml/GalleryPage.qml"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(qw);
}
