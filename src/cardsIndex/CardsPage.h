#pragma once

#include <QObject>
#include "CardsCatalog.h"
#include "CardsSearchProxy.h"

#include <QQuickWidget>

class CardsPage: public QWidget {
    Q_OBJECT
public:
    explicit CardsPage(CardsCatalog* cardsCatalog, QWidget *parent = nullptr);

private:
    void buildUi();

    CardsCatalog* cardsCatalog_;
    CardsSearchProxy *searchProxy_;
};

