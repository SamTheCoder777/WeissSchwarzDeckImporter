#include "CompareDialog.h"
#include "../database/DatabaseUtil.h"
#include "../images/CardImageProvider.h"

#include <QNetworkReply>
#include <QSqlQuery>
#include <QtWidgets>

CompareDialog::CompareDialog(const QImage& crop,
                             const std::vector<Candidate>& candidates,
                             int startIndex, QSqlDatabase& db, DatabaseUtil* dbUtil, QWidget* parent)
    : QDialog(parent), crop_(crop), cands_(candidates), db_(db), dbUtil_(dbUtil) {

    setWindowTitle("Compare & Confirm");
    setModal(true);
    resize(1100, 800);
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

    connect(dbUtil_, &DatabaseUtil::cardReady, this, [this](const QString &code) {
        if (cur_ >= 0 && cur_ < (int) cands_.size()
            && QString::fromStdString(cands_[cur_].card_id) == code) {
            showCandidate(cur_);
        }
    });

    showCandidate(startIndex >= 0 && startIndex < (int) cands_.size() ? startIndex : 0);

    setFocusPolicy(Qt::StrongFocus);
    setFocus();
}

void CompareDialog::showCandidate(int i) {
    if (cands_.empty()) return;
    if (i < 0) i = (int)cands_.size() - 1;
    if (i >= (int)cands_.size()) i = 0;
    cur_ = i;

    const Candidate& c = cands_[cur_];
    candCaption_->setText(QString("%1   ·   score %2   ·   rank %3/%4")
                              .arg(c.card_id)
                              .arg(c.score, 0, 'f', 4)
                              .arg(cur_ + 1).arg(cands_.size()));
    counterLabel_->setText(QString("Candidate %1 of %2").arg(cur_ + 1).arg(cands_.size()));

    const QString code = QString::fromStdString(c.card_id);
    const QString url = dbUtil_->imageUrlFor(code);

    curCandImage_ = QImage();
    if (url.isEmpty()) {
        showLoading(true);
        dbUtil_->ensureCardData(code);
        return;
    }

    const QString aliasPath = CardImageProvider::cacheFilePath("fallback:" + url);
    for (const QString &p : {aliasPath, CardImageProvider::cacheFilePath(url)}) {
        if (!QFile::exists(p))
            continue;
        QImage cached(p);
        if (!cached.isNull()) {
            curCandImage_ = cached;
            showLoading(false);
            rescale();
            return;
        }
    }

    showLoading(true);
    downloadImage(url, code, cur_, aliasPath, false);
}

void CompareDialog::downloadImage(
    const QString &url, const QString &code, int requested, const QString &aliasPath, bool official)
{
    qDebug() << "[CompareDialog]" << (official ? "official" : "primary") << "call to:" << url;

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "WSDeckImporter/1.0");

    QNetworkReply *r = net_.get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, url, code, requested, aliasPath, official] {
        r->deleteLater();

        QImage img;
        QByteArray bytes;
        bool ok = (r->error() == QNetworkReply::NoError);
        if (ok) {
            bytes = r->readAll();
            ok = img.loadFromData(bytes);
        }

        if (ok) {
            QStringList paths{CardImageProvider::cacheFilePath(url)};
            if (official)
                paths << aliasPath;
            for (const QString &p : paths) {
                QFile f(p);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    f.write(bytes);
            }
            if (requested != cur_)
                return;
            curCandImage_ = img;
            showLoading(false);
            rescale();
            return;
        }

        if (requested != cur_)
            return;

        if (!official) {
            tryOfficial(code, requested, aliasPath);
            return;
        }

        showLoading(false);
        candLabel_->setText("(image failed)");
    });
}

void CompareDialog::tryOfficial(const QString &code, int requested, const QString &aliasPath)
{
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(dbUtil_,
                    &DatabaseUtil::officialImageUrlReady,
                    this,
                    [this, conn, code, requested, aliasPath](const QString &c, const QString &u) {
                        if (c != code)
                            return;
                        QObject::disconnect(*conn);

                        if (requested != cur_)
                            return;
                        if (u.isEmpty()) {
                            showLoading(false);
                            candLabel_->setText("(image failed)");
                            return;
                        }

                        const QString officialPath = CardImageProvider::cacheFilePath(u);
                        if (QFile::exists(officialPath)) {
                            QImage cached(officialPath);
                            if (!cached.isNull()) {
                                if (!QFile::exists(aliasPath))
                                    QFile::copy(officialPath, aliasPath);
                                curCandImage_ = cached;
                                showLoading(false);
                                rescale();
                                return;
                            }
                        }
                        downloadImage(u, code, requested, aliasPath, true);
                    });

    dbUtil_->fetchOfficialImageUrl(code);
}

void CompareDialog::rescale() {
    if (!crop_.isNull())
        cropLabel_->setPixmap(QPixmap::fromImage(crop_)
            .scaled(cropLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (!curCandImage_.isNull())
        candLabel_->setPixmap(QPixmap::fromImage(curCandImage_)
            .scaled(candLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else if (!loading_)
        candLabel_->setText("(no master image)");
}

void CompareDialog::resizeEvent(QResizeEvent* e) {
    QDialog::resizeEvent(e);
    rescale();
}

void CompareDialog::showLoading(bool on)
{
    loading_ = on;
    candStack_->setCurrentIndex(on ? 1 : 0);
}

void CompareDialog::keyPressEvent(QKeyEvent* e) {
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
