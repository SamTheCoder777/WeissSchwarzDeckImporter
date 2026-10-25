#pragma once

#include <QUrl>
#include <QWidget>

class QWebEngineView;
class QLabel;
class QStackedWidget;

class CardBrowserWidget : public QWidget
{
public:
    explicit CardBrowserWidget(QWidget *parent = nullptr);

    void loadCard(const QUrl &url);
    void stop();

private:
    QWebEngineView *browser_ = nullptr;

    QStackedWidget *pageStack_ = nullptr;
    QLabel *loadingLabel_ = nullptr;
};