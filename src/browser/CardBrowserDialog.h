#pragma once

#include <QDialog>
#include <QImage>
#include <QUrl>

class QLabel;
class QEvent;
class QWebEngineView;

class CardBrowserDialog : public QDialog
{
public:
    explicit CardBrowserDialog(QWidget *parent = nullptr);

    void showCard(const QString &cardCode, const QUrl &url, const QImage &crop = QImage());

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rescaleCrop();

    QImage crop_;
    QLabel *cropLabel_ = nullptr;
    QWidget *imagePanel_ = nullptr;
    QWebEngineView *browser_ = nullptr;
};