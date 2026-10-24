#include "CardBrowserDialog.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSizePolicy>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

CardBrowserDialog::CardBrowserDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Card reference"));
    resize(1100, 850);

    auto *root = new QVBoxLayout(this);
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    root->addWidget(splitter);

    // Left: compare card
    imagePanel_ = new QWidget(splitter);
    auto *left = imagePanel_;
    auto *leftLayout = new QVBoxLayout(left);

    auto *caption = new QLabel(QStringLiteral("Your crop"), left);
    caption->setAlignment(Qt::AlignCenter);
    leftLayout->addWidget(caption);

    cropLabel_ = new QLabel(left);
    cropLabel_->setAlignment(Qt::AlignCenter);
    cropLabel_->setMinimumSize(240, 320);
    cropLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    cropLabel_->setStyleSheet("background:#111318;");
    cropLabel_->installEventFilter(this);
    leftLayout->addWidget(cropLabel_, 1);

    // Right: website
    auto *right = new QWidget(splitter);
    right->setMinimumWidth(360);
    auto *rightLayout = new QVBoxLayout(right);

    auto *status = new QLabel(right);
    rightLayout->addWidget(status);

    browser_ = new QWebEngineView(right);
    rightLayout->addWidget(browser_, 1);

    static auto *profile = new QWebEngineProfile(QStringLiteral("OfficialCardBrowser"), qApp);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
    profile->setHttpUserAgent(QStringLiteral("Mozilla/5.0 (Linux; Android 13; Pixel 7) "
                                             "AppleWebKit/537.36 (KHTML, like Gecko) "
                                             "Chrome/120.0.0.0 Mobile Safari/537.36"));

    browser_->setPage(new QWebEnginePage(profile, browser_));

    auto *buttons = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("Back"), right);
    auto *reload = new QPushButton(QStringLiteral("Reload"), right);
    auto *close = new QPushButton(QStringLiteral("Close"), right);

    buttons->addWidget(back);
    buttons->addWidget(reload);
    buttons->addStretch();
    buttons->addWidget(close);
    rightLayout->addLayout(buttons);

    connect(back, &QPushButton::clicked, browser_, &QWebEngineView::back);

    connect(reload, &QPushButton::clicked, browser_, &QWebEngineView::reload);

    connect(close, &QPushButton::clicked, this, &QDialog::close);

    connect(browser_, &QWebEngineView::loadStarted, this, [status] {
        status->setText(QStringLiteral("Loading…"));
    });

    connect(browser_, &QWebEngineView::loadFinished, this, [status](bool ok) {
        status->setText(ok ? QString() : QStringLiteral("Page failed to load."));
    });

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setSizes({1200, 820});
}

void CardBrowserDialog::showCard(const QString &cardCode, const QUrl &url, const QImage &crop)
{
    if (!url.isValid() || url.isEmpty())
        return;

    browser_->stop();

    crop_ = crop.copy();
    cropLabel_->clear();
    imagePanel_->setVisible(!crop_.isNull());

    setWindowTitle(QStringLiteral("Card reference — %1").arg(cardCode));

    show();
    rescaleCrop();
    raise();
    activateWindow();

    browser_->load(url);
}

void CardBrowserDialog::rescaleCrop()
{
    if (crop_.isNull()) {
        cropLabel_->setText(QStringLiteral("(no crop available)"));
        return;
    }

    cropLabel_->setPixmap(QPixmap::fromImage(crop_).scaled(cropLabel_->contentsRect().size(),
                                                           Qt::KeepAspectRatio,
                                                           Qt::SmoothTransformation));
}

bool CardBrowserDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == cropLabel_
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        rescaleCrop();
    }

    return QDialog::eventFilter(watched, event);
}