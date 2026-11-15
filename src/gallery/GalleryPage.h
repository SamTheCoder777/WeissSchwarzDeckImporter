#pragma once
#include <QWidget>

class DatabaseUtil;
class SelectionModel;
class UiBridge;
class DeckBuilderController;
class GalleryController;
class QQuickWidget;

class GalleryPage : public QWidget
{
    Q_OBJECT
public:
    explicit GalleryPage(DatabaseUtil *database,
                         SelectionModel *selection,
                         UiBridge *bridge,
                         DeckBuilderController *deck,
                         QWidget *parent = nullptr);
    void showDeck();

private:
    QQuickWidget *view_;
    GalleryController *controller_;
};
