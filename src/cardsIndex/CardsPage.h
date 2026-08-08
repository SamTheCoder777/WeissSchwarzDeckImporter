#pragma once

#include <QWidget>
#include "SeriesCatalog.h"
#include "SeriesSearchProxy.h"

class QQuickWidget;

class CardsPage : public QWidget {
    Q_OBJECT
public:
    explicit CardsPage(SeriesCatalog* catalog, QWidget* parent = nullptr);

private:
    void buildUi();

    SeriesCatalog*      catalog_;
    SeriesSearchProxy*  searchProxy_;
};