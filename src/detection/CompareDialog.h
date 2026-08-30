// CompareDialog.h — big side-by-side compare + confirm window.
//
// Left: the user's crop (large). Right: one candidate at a time (large master art),
// with Left/Right arrows to cycle candidates and Enter to confirm the shown one.
// Returns via confirmedIndex(): the chosen candidate index, or -1 if cancelled.
#pragma once

#include <QDialog>
#include <QImage>
#include <QProgressBar>
#include <QSqlDatabase>
#include <QStackedWidget>
#include <QtNetwork>
#include "../database/DatabaseUtil.h"
#include "../models/tcg_infer.h"
#include <vector>

class QLabel;
class QPushButton;

class CompareDialog : public QDialog {
    Q_OBJECT
public:
    CompareDialog(const QImage& crop,
                  const std::vector<Candidate>& candidates,
                  int startIndex, QSqlDatabase& db, DatabaseUtil* dbUtil,
                  QWidget* parent = nullptr);

    int confirmedIndex() const { return confirmed_; }

protected:
    void keyPressEvent(QKeyEvent*) override;      // Left/Right/Enter/Esc
    void resizeEvent(QResizeEvent*) override;     // rescale images to fit

private:
    void showCandidate(int i);
    void rescale();
    void showLoading(bool on);

    QImage crop_;
    std::vector<Candidate> cands_;
    int cur_ = 0;
    int confirmed_ = -1;

    QLabel* cropLabel_;
    QLabel* candLabel_;
    QLabel* cropCaption_;
    QLabel* candCaption_;
    QLabel* counterLabel_;
    QPushButton* confirmBtn_;
    QImage curCandImage_;

    QSqlDatabase db_;
    QNetworkAccessManager net_;
    QString imageUrlFor(QString cardId);
    DatabaseUtil* dbUtil_ = nullptr;

    QProgressBar *busyBar_ = nullptr;
    QStackedWidget *candStack_ = nullptr;
    bool loading_ = false;
};
