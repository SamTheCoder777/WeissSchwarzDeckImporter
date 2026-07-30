#include "CompareDialog.h"
#include "database/DatabaseUtil.h"

#include <QNetworkReply>
#include <QSqlQuery>
#include <QtWidgets>

// toDeckCode lives in Models.cpp
QString toDeckCode(const std::string& cardId);

CompareDialog::CompareDialog(const QImage& crop,
                             const std::vector<Candidate>& candidates,
                             int startIndex, QSqlDatabase& db, QWidget* parent)
    : QDialog(parent), crop_(crop), cands_(candidates), db_(db) {

    setWindowTitle("Compare & Confirm");
    setModal(true);
    resize(1100, 800);
    setStyleSheet("QDialog{background:#1b1d21;} QLabel{color:#e8eaed;}");

    auto* root = new QVBoxLayout(this);

    // ── two big image panels side by side ──────────────────────────────────
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
    rightCol->addWidget(candCaption_);
    rightCol->addWidget(candLabel_, 1);

    row->addLayout(leftCol, 1);
    row->addLayout(rightCol, 1);
    root->addLayout(row, 1);

    // ── nav + confirm bar ──────────────────────────────────────────────────
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

    // show the crop (fixed) and the starting candidate
    showCandidate(startIndex >= 0 && startIndex < (int)cands_.size() ? startIndex : 0);

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
                              .arg(toDeckCode(c.card_id))
                              .arg(c.score, 0, 'f', 4)
                              .arg(cur_ + 1).arg(cands_.size()));
    counterLabel_->setText(QString("Candidate %1 of %2").arg(cur_ + 1).arg(cands_.size()));

    // resolve the SAME url the results panel uses
    QString url = DatabaseUtil::imageUrlFor(QString::fromStdString(c.card_id));

    curCandImage_ = QImage();          // clear old image
    if (url.isEmpty()) { candLabel_->setText("(no image for this card)"); return; }

    candLabel_->setText("loading…");
    const int requested = cur_;        // capture which card we asked for
    QNetworkReply* r = net_.get(QNetworkRequest(QUrl(url)));
    connect(r, &QNetworkReply::finished, this, [this, r, requested]{
        r->deleteLater();
        if (requested != cur_) return;                 // user already moved on
        if (r->error() != QNetworkReply::NoError) { candLabel_->setText("(image failed)"); return; }
        curCandImage_.loadFromData(r->readAll());       // decode downloaded bytes
        rescale();
    });
}

void CompareDialog::rescale() {
    if (!crop_.isNull())
        cropLabel_->setPixmap(QPixmap::fromImage(crop_)
            .scaled(cropLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (!curCandImage_.isNull())
        candLabel_->setPixmap(QPixmap::fromImage(curCandImage_)
            .scaled(candLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else
        candLabel_->setText("(no master image)");
}

void CompareDialog::resizeEvent(QResizeEvent* e) {
    QDialog::resizeEvent(e);
    rescale();      // keep both images filling their panels
}

void CompareDialog::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Left:  showCandidate(cur_ - 1); return;
    case Qt::Key_Right: showCandidate(cur_ + 1); return;
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
