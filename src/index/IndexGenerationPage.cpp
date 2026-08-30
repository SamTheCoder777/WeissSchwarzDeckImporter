#include "IndexGenerationPage.h"

#include <QQmlContext>
#include <QVBoxLayout>

#include "../core/Config.h"

IndexGenerationPage::IndexGenerationPage(ModelService *models, QWidget *parent)
    : models_(models)
    , QWidget(parent)
{
    buildUi();
}

void IndexGenerationPage::buildUi()
{
    auto *qw = new QQuickWidget(this);
    qw->rootContext()->setContextProperty("models", models_);
    qw->rootContext()->setContextProperty("config", &Config::instance());
    qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qw->setSource(QUrl("qrc:/qml/IndexGenerationPage.qml"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(qw);
}
