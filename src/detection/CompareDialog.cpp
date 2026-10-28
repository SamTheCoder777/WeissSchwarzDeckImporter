#include "CompareDialog.h"

#include <QSqlQuery>
#include <QtWidgets>

#include "../browser/CardBrowserWidget.h"
#include "../database/DatabaseUtil.h"
#include "../images/CardImageProvider.h"
#include "../network/NetGuard.h"

CompareDialog::CompareDialog(const QImage& crop,
                             const std::vector<Candidate>& candidates,
                             int startIndex, QSqlDatabase& db, DatabaseUtil* dbUtil, QWidget* parent)
    : QDialog(parent), crop_(crop), cands_(candidates), db_(db), dbUtil_(dbUtil) {

    setWindowTitle("Compare & Confirm");
    setModal(true);
    resize(900, 800);
    setStyleSheet("QDialog{background:#1b1d21;} QLabel{color:#e8eaed;}");

    auto* root = new QVBoxLayout(this);

    auto* row = new QHBoxLayout;

    auto* leftCol = new QVBoxLayout;
    cropCaption_ = new QLabel("Your crop");
    cropCaption_->setAlignment(Qt::AlignCenter);
    cropCaption_->setStyleSheet("font-size:14px; color:#9aa0a6;");
    cropLabel_ = new QLabel;
    cropLabel_->setAlignment(Qt::AlignCenter);
    cropLabel_->setMinimumSize(360, 500);
    cropLabel_->setStyleSheet("background:#111318; border-radius:8px;");
    leftCol->addWidget(cropCaption_);
    leftCol->addWidget(cropLabel_, 1);

    auto* rightCol = new QVBoxLayout;
    candCaption_ = new QLabel;
    candCaption_->setAlignment(Qt::AlignCenter);
    candCaption_->setStyleSheet("font-size:14px; color:#e8eaed; font-weight:bold;");
    candLabel_ = new QLabel;
    candLabel_->setAlignment(Qt::AlignCenter);
    candLabel_->setMinimumSize(360, 500);
    candLabel_->setStyleSheet("background:#111318; border-radius:8px;");

    candStack_ = new QStackedWidget;
    candStack_->addWidget(candLabel_);

    QWidget *busyPage = new QWidget;
    auto *bl = new QVBoxLayout(busyPage);
    bl->addStretch();

    busyBar_ = new QProgressBar;
    busyBar_->setRange(0, 0);
    busyBar_->setTextVisible(false);
    busyBar_->setFixedHeight(4);
    busyBar_->setMinimumWidth(200);
    busyBar_->setMaximumWidth(400);
    busyBar_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *barRow = new QHBoxLayout;
    barRow->addStretch();
    barRow->addWidget(busyBar_, 1);
    barRow->addStretch();

    bl->addLayout(barRow);
    bl->addStretch();
    candStack_->addWidget(busyPage);
    browserPanel_ = new CardBrowserWidget(candStack_);
    candStack_->addWidget(browserPanel_);

    rightCol->addWidget(candCaption_);
    rightCol->addWidget(candStack_, 1);

    row->addLayout(leftCol, 1);
    row->addLayout(rightCol, 1);
    root->addLayout(row, 1);

    auto* bar = new QHBoxLayout;
    auto* prevBtn = new QPushButton("◀  Prev  (←)");
    auto* nextBtn = new QPushButton("Next  (→)  ▶");
    counterLabel_ = new QLabel;
    counterLabel_->setAlignment(Qt::AlignCenter);
    counterLabel_->setStyleSheet("color:#9aa0a6;");
    confirmBtn_ = new QPushButton("Confirm this card  (Enter)");
    confirmBtn_->setStyleSheet(
        "QPushButton{background:#3ecf7a;color:white;font-weight:bold;"
        "border-radius:7px;padding:10px 18px;}"
        "QPushButton:hover{background:#4ad885;}");

    auto* btnStyle =
        "QPushButton{background:#2e3238;color:#e8eaed;border-radius:7px;padding:10px 16px;}"
        "QPushButton:hover{background:#3a3f46;}";
    prevBtn->setStyleSheet(btnStyle);
    nextBtn->setStyleSheet(btnStyle);

    bar->addWidget(prevBtn);
    bar->addWidget(counterLabel_, 1);
    bar->addWidget(nextBtn);
    bar->addSpacing(20);
    bar->addWidget(confirmBtn_);
    root->addLayout(bar);

    prevBtn->setFocusPolicy(Qt::NoFocus);
    nextBtn->setFocusPolicy(Qt::NoFocus);
    confirmBtn_->setFocusPolicy(Qt::NoFocus);

    confirmBtn_->setAutoDefault(false);
    confirmBtn_->setDefault(false);

    connect(prevBtn, &QPushButton::clicked, this, [this]{ showCandidate(cur_ - 1); });
    connect(nextBtn, &QPushButton::clicked, this, [this]{ showCandidate(cur_ + 1); });
    connect(confirmBtn_, &QPushButton::clicked, this, [this]{
        confirmed_ = cur_;
        accept();
    });

    qApp->installEventFilter(this);
    showCandidate(startIndex >= 0 && startIndex < (int) cands_.size() ? startIndex : 0);

    setFocusPolicy(Qt::StrongFocus);
    setFocus();
}

void CompareDialog::tryNextImage(const QStringList &urls, int index, quint64 generation)
{
    if (generation != imageGeneration_)
        return;

    const QString requestedCode = QString::fromStdString(cands_[cur_].card_id);

    while (index < urls.size()) {
        const QString url = urls.at(index++);

        if (!dbUtil_ || !dbUtil_->hasExactImageUrl(requestedCode, url)) {
            showOfficialPage(requestedCode);
            return;
        }

        const QString cachePath = CardImageProvider::cacheFilePath(url);

        QImage cached(cachePath);

        if (!cached.isNull()) {
            qDebug() << "[IMAGE][Compare][DISK]" << requestedCode << url;

            dbUtil_->reportLoadedImage(requestedCode, url);
            CardImageProvider::rememberImage(requestedCode, cached);

            showLoadedImage(requestedCode, cached);
            return;
        }

        if (dbUtil_->imageRetryBlocked(url)) {
            qDebug() << "[IMAGE][Compare][SKIP MISSING DB]" << requestedCode << url;
            continue;
        }

        NetRequest request;
        request.url = QUrl(url);
        request.key = QStringLiteral("card-image:") + url;
        request.useCache = false;
        request.timeoutMs = 15000;
        request.retries = 0;

        NetGuard::instance().get(request,
                                 this,
                                 [this, urls, index, generation, requestedCode, url, cachePath](
                                     const NetResult &result) {
                                     if (generation != imageGeneration_)
                                         return;

                                     if (result.status == NetResult::Cancelled)
                                         return;

                                     QImage image;
                                     if (result.ok())
                                         image.loadFromData(result.body);

                                     if (!image.isNull()) {
                                         CardImageProvider::rememberImage(requestedCode, image);

                                         if (dbUtil_)
                                             dbUtil_->reportLoadedImage(requestedCode, url);

                                         QSaveFile file(cachePath);

                                         if (!file.open(QIODevice::WriteOnly)) {
                                             qWarning() << "Image cache open failed:" << cachePath
                                                        << file.errorString();
                                         } else if (file.write(result.body) != result.body.size()) {
                                             qWarning() << "Image cache write failed:" << cachePath
                                                        << file.errorString();
                                             file.cancelWriting();
                                         } else if (!file.commit()) {
                                             qWarning() << "Image cache commit failed:" << cachePath
                                                        << file.errorString();
                                         }

                                         showLoadedImage(requestedCode, image);
                                         return;
                                     }

                                     if (!result.blocked && result.status == NetResult::ClientError
                                         && (result.http == 404 || result.http == 410) && dbUtil_) {
                                         dbUtil_->recordImageFailure(url, result.http);
                                     }

                                     qWarning()
                                         << "[IMAGE][Compare][FAILED]" << requestedCode << url
                                         << (result.ok() ? QStringLiteral("Invalid image data")
                                                         : result.message);

                                     tryNextImage(urls, index, generation);
                                 });

        return;
    }

    showOfficialPage(requestedCode);
}

void CompareDialog::showCandidate(int i)
{
    if (cands_.empty())
        return;
    if (i < 0)
        i = (int) cands_.size() - 1;
    if (i >= (int) cands_.size())
        i = 0;
    cur_ = i;

    const Candidate &c = cands_[cur_];
    candCaption_->setText(QString("%1   ·   score %2   ·   rank %3/%4")
                              .arg(c.card_id)
                              .arg(c.score, 0, 'f', 4)
                              .arg(cur_ + 1)
                              .arg(cands_.size()));
    counterLabel_->setText(QString("Candidate %1 of %2").arg(cur_ + 1).arg(cands_.size()));

    const QString code = QString::fromStdString(c.card_id);

    const quint64 generation = ++imageGeneration_;
    browserPanel_->stop();

    curCandImage_ = QImage();
    candLabel_->clear();

    if (requiresOfficialData(code)) {
        showOfficialPage(code);
        return;
    }

    const QImage memory = CardImageProvider::memoryImage(code);

    if (!memory.isNull()) {
        qDebug() << "[IMAGE][Compare][MEMORY]" << code;

        showLoadedImage(code, memory);
        return;
    }

    showLoading(true);

    const QStringList urls = dbUtil_ ? dbUtil_->imageUrlsFor(code) : QStringList();

    tryNextImage(urls, 0, generation);
}

void CompareDialog::rescale()
{
    if (!crop_.isNull())
        cropLabel_->setPixmap(QPixmap::fromImage(crop_).scaled(cropLabel_->size(),
                                                               Qt::KeepAspectRatio,
                                                               Qt::SmoothTransformation));
    if (!curCandImage_.isNull())
        candLabel_->setPixmap(
            QPixmap::fromImage(curCandImage_)
                .scaled(candLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else if (!loading_)
        candLabel_->setText("(no master image)");
}

void CompareDialog::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    rescale();
}

void CompareDialog::showLoading(bool on)
{
    loading_ = on;
    candStack_->setCurrentIndex(on ? 1 : 0);
}

bool CompareDialog::requiresOfficialData(const QString &code) const
{
    if (!dbUtil_)
        return true;

    const QVariantMap data = dbUtil_->cardDataFor(code);

    return data.isEmpty() || data.value("dataFallbackUsed").toBool();
}

void CompareDialog::showOfficialPage(const QString &code)
{
    loading_ = false;
    curCandImage_ = QImage();

    candStack_->setCurrentWidget(browserPanel_);

    if (dbUtil_) {
        browserPanel_->loadCard(QUrl(dbUtil_->officialCardPageUrl(code)));
    }
}

void CompareDialog::showLoadedImage(const QString &code, const QImage &image)
{
    if (dbUtil_ && dbUtil_->imageUsesOtherRarity(code)) {
        showOfficialPage(code);
        return;
    }

    curCandImage_ = image;
    showLoading(false);
    rescale();
}

bool CompareDialog::eventFilter(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);

    if (!isVisible() || !isActiveWindow() || !widget || (widget != this && !isAncestorOf(widget))) {
        return QDialog::eventFilter(watched, event);
    }

    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress) {
        return QDialog::eventFilter(watched, event);
    }

    auto *key = static_cast<QKeyEvent *>(event);

    if (key->modifiers() != Qt::NoModifier)
        return QDialog::eventFilter(watched, event);

    if (key->key() != Qt::Key_Left && key->key() != Qt::Key_Right && key->key() != Qt::Key_Escape) {
        return QDialog::eventFilter(watched, event);
    }

    key->accept();

    if (event->type() == QEvent::KeyPress) {
        if (key->key() == Qt::Key_Escape)
            reject();
        else
            showCandidate(cur_ + (key->key() == Qt::Key_Right ? 1 : -1));
    }

    return true;
}

void CompareDialog::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_A:
        showCandidate(cur_ - 1);
        return;
    case Qt::Key_Right:
    case Qt::Key_D:
        showCandidate(cur_ + 1);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        confirmed_ = cur_;
        accept();
        return;
    case Qt::Key_Escape:
        reject();
        return;
    }
    QDialog::keyPressEvent(e);
}

void CompareDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    rescale();
}
