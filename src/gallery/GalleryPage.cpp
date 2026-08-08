#include "GalleryPage.h"

#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>
#include <QVBoxLayout>

GalleryPage::GalleryPage(DatabaseUtil *dbUtil, SelectionModel *selModel, UiBridge *bridge,
                         TranslationWorker* translationWorker, QWidget *parent):
    dbUtil_(dbUtil), selModel_(selModel), bridge_(bridge), translationWorker_(translationWorker), QWidget(parent)
{
    buildUi();
}

void GalleryPage::buildUi()
{
    // QString translation = translationWorker_->translate("【永】 あなたの<TRAIT>のキャラが4枚以上なら、このカードは、色条件を満たさずに手札からプレイでき、あなたの手札のこのカードのレベルを－1。");

    // qDebug() << "Translation: " << translation;

    // translation = translationWorker_->translate("【自】 相手のキャラが、思い出になった時、あなたは自分のキャラを1枚選び、そのターン中、パワーを＋2500。\n"
    //                                            "【起】［(1) あなたのキャラを2枚【レスト】する］ あなたは自分の山札を上から5枚まで見て、レベル1以上のカードを1枚まで選んで相手に見せ、手札に加え、残りのカードを控え室に置く。（CXのレベルは0として扱う）");

    // qDebug() << "Translation: " << translation;

    auto* qw = new QQuickWidget(this);
    qw->rootContext()->setContextProperty("cardDatabase", dbUtil_);
    qw->rootContext()->setContextProperty("selModel", selModel_);
    qw->rootContext()->setContextProperty("bridge", bridge_);
    qw->rootContext()->setContextProperty("translationWorker", translationWorker_);
    qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qw->setSource(QUrl("qrc:/qml/gallery/GalleryPage.qml"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(qw);
}
