#include "DeckBuilderPage.h"

#include <QApplication>
#include <QGuiApplication>
#include <QKeySequence>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QShortcut>
#include <QVBoxLayout>
#include <QVariant>

#include "DeckBuilderController.h"
#include "DeckBuilderImages.h"

DeckBuilderPage::DeckBuilderPage(DeckBuilderController *controller, QWidget *parent)
    : QWidget(parent)
    , controller_(controller)
{
    Q_ASSERT(controller_);
    view_ = new QQuickWidget(this);
    view_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view_->engine()->addImageProvider("deckcards", new DeckBuilderImageProvider);
    view_->rootContext()->setContextProperty("deckBuilder", controller_);
    view_->setSource(QUrl("qrc:/qml/deckbuilder/DeckBuilderPage.qml"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view_);

    auto textEditorHasFocus = [this] {
        QObject *focus = view_->quickWindow()
                             ? static_cast<QObject *>(view_->quickWindow()->activeFocusItem())
                             : QGuiApplication::focusObject();
        while (focus) {
            const QVariant readOnly = focus->property("readOnly");
            if (readOnly.isValid() && !readOnly.toBool()
                && (focus->property("text").isValid() || focus->property("plainText").isValid())) {
                return true;
            }
            auto *item = qobject_cast<QQuickItem *>(focus);
            focus = item ? item->parentItem() : nullptr;
        }
        return false;
    };

    auto *undoShortcut = new QShortcut(QKeySequence("Ctrl+Z"), this);
    undoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    undoShortcut->setAutoRepeat(false);
    connect(undoShortcut, &QShortcut::activated, this, [this, textEditorHasFocus] {
        if (!isVisible() || QApplication::activeModalWidget() || textEditorHasFocus())
            return;
        controller_->undoDeckEdit();
    });

    auto *redoShortcut = new QShortcut(QKeySequence("Ctrl+Shift+Z"), this);
    redoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    redoShortcut->setAutoRepeat(false);
    connect(redoShortcut, &QShortcut::activated, this, [this, textEditorHasFocus] {
        if (!isVisible() || QApplication::activeModalWidget() || textEditorHasFocus())
            return;
        controller_->redoDeckEdit();
    });
}
bool DeckBuilderPage::confirmClose()
{
    return controller_->confirmDiscard();
}
