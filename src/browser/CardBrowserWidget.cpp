#include "CardBrowserWidget.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

CardBrowserWidget::CardBrowserWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *status = new QLabel(this);
    layout->addWidget(status);

    pageStack_ = new QStackedWidget(this);
    layout->addWidget(pageStack_, 1);

    loadingLabel_ = new QLabel(QStringLiteral("Loading official card page…"), pageStack_);
    loadingLabel_->setAlignment(Qt::AlignCenter);
    loadingLabel_->setStyleSheet("background:#111318; color:#aab4c2; font-size:14px;");
    pageStack_->addWidget(loadingLabel_);

    browser_ = new QWebEngineView(pageStack_);
    browser_->setMinimumSize(0, 0);
    pageStack_->addWidget(browser_);

    pageStack_->setCurrentWidget(loadingLabel_);

    QWebEngineProfile *profile = nullptr;

    const auto profiles = qApp->findChildren<QWebEngineProfile *>();
    for (auto *candidate : profiles) {
        if (candidate->storageName() == QStringLiteral("OfficialCardBrowser")) {
            profile = candidate;
            break;
        }
    }

    if (!profile) {
        profile = new QWebEngineProfile(QStringLiteral("OfficialCardBrowser"), qApp);

        profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);

        profile->setHttpUserAgent(QStringLiteral("Mozilla/5.0 (Linux; Android 13; Pixel 7) "
                                                 "AppleWebKit/537.36 (KHTML, like Gecko) "
                                                 "Chrome/120.0.0.0 Mobile Safari/537.36"));
    }

    browser_->setPage(new QWebEnginePage(profile, browser_));

    auto *buttons = new QHBoxLayout;
    auto *reload = new QPushButton(QStringLiteral("Reload page"), this);
    buttons->addStretch();
    buttons->addWidget(reload);
    layout->addLayout(buttons);

    connect(reload, &QPushButton::clicked, browser_, &QWebEngineView::reload);

    connect(browser_, &QWebEngineView::loadStarted, this, [this, status] {
        status->setText(QStringLiteral("Loading…"));
        loadingLabel_->setText(QStringLiteral("Loading official card page…"));
        pageStack_->setCurrentWidget(loadingLabel_);
    });

    connect(browser_, &QWebEngineView::loadFinished, this, [this, status](bool ok) {
        if (ok) {
            status->clear();
            pageStack_->setCurrentWidget(browser_);
        } else {
            status->clear();
            loadingLabel_->setText(QStringLiteral("Page unavailable. Click Reload page."));
            pageStack_->setCurrentWidget(loadingLabel_);
        }
    });
}

void CardBrowserWidget::loadCard(const QUrl &url)
{
    stop();

    loadingLabel_->setText(QStringLiteral("Loading official card page…"));
    pageStack_->setCurrentWidget(loadingLabel_);

    browser_->load(url);
}

void CardBrowserWidget::stop()
{
    browser_->stop();

    // Notify user that we are loading new card
    pageStack_->setCurrentWidget(loadingLabel_);
}