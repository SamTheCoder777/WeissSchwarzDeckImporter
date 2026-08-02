// UiBridge.h — the QML <-> C++ boundary. QML calls these; MainWindow reacts.
#pragma once

#include <QObject>
#include <QString>

class UiBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString summaryText   READ summaryText   NOTIFY stateChanged)
    Q_PROPERTY(QString confirmedText READ confirmedText NOTIFY stateChanged)
    Q_PROPERTY(bool    isConfirmed   READ isConfirmed   NOTIFY stateChanged)
    Q_PROPERTY(int     quantity      READ quantity      NOTIFY stateChanged)
    Q_PROPERTY(int     currentIndex  READ currentIndex  NOTIFY stateChanged)
    Q_PROPERTY(int     cropRev       READ cropRev       NOTIFY stateChanged)
    Q_PROPERTY(bool    modelLoaded   READ modelLoaded   NOTIFY stateChanged)
public:
    using QObject::QObject;

    // ── called FROM QML ────────────────────────────────────────────────────
    Q_INVOKABLE void selectCard(int index)      { emit selectCardRequested(index); }
    Q_INVOKABLE void confirm(int candIndex)     { emit confirmRequested(candIndex); }
    Q_INVOKABLE void setQuantity(int qty)       { emit quantityRequested(qty); }
    Q_INVOKABLE void exportDeck()               { emit exportRequested(); }
    Q_INVOKABLE void runDetection()             { emit detectRequested(); }
    Q_INVOKABLE void openCompare() { emit openCompareRequested(); }

    // ── state pushed FROM C++ ──────────────────────────────────────────────
    QString summaryText()   const { return summary_; }
    QString confirmedText() const { return confirmed_; }
    bool    isConfirmed()   const { return isConfirmed_; }
    int     quantity()      const { return qty_; }
    int     currentIndex()  const { return current_; }
    int     cropRev()       const { return cropRev_; }
    bool    modelLoaded()   const { return modelLoaded_; }

    void setState(const QString& summary, const QString& confirmedText,
                  bool isConfirmed, int qty, int current, bool modelLoaded) {
        summary_ = summary; confirmed_ = confirmedText; isConfirmed_ = isConfirmed;
        qty_ = qty; current_ = current; modelLoaded_ = modelLoaded;
        emit stateChanged();
    }
    void bumpCrop() { ++cropRev_; emit stateChanged(); }

signals:
    void stateChanged();
    void selectCardRequested(int index);
    void confirmRequested(int candIndex);
    void quantityRequested(int qty);
    void exportRequested();
    void detectRequested();
    void openCompareRequested();

private:
    QString summary_ = "0 selected · 0 confirmed";
    QString confirmed_ = "Not confirmed";
    bool isConfirmed_ = false, modelLoaded_ = false;
    int qty_ = 1, current_ = -1, cropRev_ = 0;
};
