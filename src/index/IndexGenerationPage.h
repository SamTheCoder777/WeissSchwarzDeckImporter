#pragma once

#include <QQuickWidget>

#include "../services/ModelService.h"

class IndexGenerationPage : public QWidget
{
public:
    explicit IndexGenerationPage(ModelService *models, QWidget *parent = nullptr);

private:
    void buildUi();

    ModelService *models_;
};
