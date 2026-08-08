#include "CardsPage.h"


#include "../core/Config.h"

#include <QMessageBox>
#include <QQmlContext>
#include <QQmlEngine>
#include <QVBoxLayout>

CardsPage::CardsPage(CardsCatalog* cardsCatalog, QWidget *parent): cardsCatalog_(cardsCatalog), QWidget(parent) {
    searchProxy_ = new CardsSearchProxy(this);
    searchProxy_->setSourceModel(cardsCatalog);

    buildUi();

}

void CardsPage::buildUi()
{
    auto* qw = new QQuickWidget(this);
    qw->rootContext()->setContextProperty("catalog", cardsCatalog_);
    qw->rootContext()->setContextProperty("indexList", searchProxy_);
    qw->rootContext()->setContextProperty("config", &Config::instance());
    qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qw->setSource(QUrl("qrc:/qml/CardsDownloadPage.qml"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(qw);

    connect(cardsCatalog_, &CardsCatalog::errorOccurred, this, [this](const QString& msg) {
        QMessageBox::critical(this, "Index Catalog Error", msg);
    });
}

