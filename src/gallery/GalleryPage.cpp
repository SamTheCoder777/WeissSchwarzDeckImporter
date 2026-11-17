#include "GalleryPage.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVBoxLayout>

#include "../database/DatabaseUtil.h"
#include "../deckbuilder/DeckBuilderController.h"
#include "../deckbuilder/DeckBuilderImages.h"
#include "../images/CardImageProvider.h"
#include "../viewmodels/Models.h"
#include "../viewmodels/UiBridge.h"
#include "GalleryController.h"

GalleryPage::GalleryPage(DatabaseUtil *database,
                         SelectionModel *selection,
                         UiBridge *bridge,
                         DeckBuilderController *deck,
                         QWidget *parent)
    : QWidget(parent)
    , view_(new QQuickWidget(this))
    , controller_(new GalleryController(database, selection, deck, this))
{
    Q_ASSERT(deck);
    auto *context = view_->rootContext();
    context->setContextProperty("cardDatabase", database);
    context->setContextProperty("selModel", selection);
    context->setContextProperty("bridge", bridge);
    context->setContextProperty("deckBuilder", deck);
    context->setContextProperty("galleryController", controller_);
    view_->engine()->addImageProvider("cardcache", new CardImageProvider(database));
    view_->engine()->addImageProvider("deckcards", new DeckBuilderImageProvider);
    view_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view_->setSource(QUrl("qrc:/qml/gallery/GalleryWorkspace.qml"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view_);
}

void GalleryPage::showDeck()
{
    if (view_->rootObject())
        QMetaObject::invokeMethod(view_->rootObject(), "showDeck");
}
