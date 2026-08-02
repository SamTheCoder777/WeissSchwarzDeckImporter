#pragma once

#include "IndexCatalog.h"
#include "IndexSearchProxy.h"
#include "../services/ModelService.h"

#include <QQuickWidget>

class FaissPage: public QWidget {
    Q_OBJECT

public:
    explicit FaissPage(ModelService* models, IndexCatalog* catalog, QWidget* parent = nullptr);

private:
    void buildUi();

    ModelService* models_;
    IndexCatalog* catalog_;
    IndexSearchProxy *searchProxy_;
};