#pragma once
#include <QWidget>
class DeckBuilderController;
class QQuickWidget;

class DeckBuilderPage final : public QWidget
{
    Q_OBJECT
public:
    explicit DeckBuilderPage(DeckBuilderController *controller, QWidget *parent = nullptr);
    bool confirmClose();

private:
    DeckBuilderController *controller_;
    QQuickWidget *view_;
};
