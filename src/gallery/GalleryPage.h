#pragma once

#include "../database/DatabaseUtil.h"
#include "../viewmodels/Models.h"
#include "../viewmodels/UiBridge.h"
#include "../translate/TranslationWorker.h"

#include <QWidget>

class GalleryPage: public QWidget {
    Q_OBJECT

public:
    explicit GalleryPage(DatabaseUtil* dbUtil,SelectionModel* selModel, UiBridge* bridge, TranslationWorker* translationWorker,
                           QWidget* parent = nullptr);

private:
    void buildUi();

    DatabaseUtil* dbUtil_;
    SelectionModel* selModel_;
    UiBridge* bridge_;
    TranslationWorker* translationWorker_;
};

