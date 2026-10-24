#pragma once

#include <QDialog>
#include <QImage>
#include <QProgressBar>
#include <QSqlDatabase>
#include <QStackedWidget>
#include <QStringList>
#include <QtNetwork>
#include <vector>

#include "../database/DatabaseUtil.h"
#include "../models/tcg_infer.h"

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
    void keyPressEvent(QKeyEvent *) override;  // Left/Right/Enter/Esc
    void resizeEvent(QResizeEvent *) override; // rescale images to fit
    void showEvent(QShowEvent *e)
        override; // For bugfix: https://github.com/SamTheCoder777/WeissSchwarzDeckImporter/issues/6

private:
    void showCandidate(int i);
    void rescale();
    void showLoading(bool on);

    void tryNextImage(const QStringList &urls, int index, quint64 generation);

    QImage crop_;
    std::vector<Candidate> cands_;
    int cur_ = 0;
    int confirmed_ = -1;
    quint64 imageGeneration_ = 0;

    QLabel* cropLabel_;
    QLabel* candLabel_;
    QLabel* cropCaption_;
    QLabel* candCaption_;
    QLabel* counterLabel_;
    QPushButton* confirmBtn_;
    QImage curCandImage_;

    QSqlDatabase db_;
    QNetworkAccessManager net_;
    DatabaseUtil* dbUtil_ = nullptr;

    QProgressBar *busyBar_ = nullptr;
    QStackedWidget *candStack_ = nullptr;
    bool loading_ = false;
};
