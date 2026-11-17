#pragma once
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class DatabaseUtil;
class SelectionModel;
class DeckBuilderController;

class GalleryController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList detectedRows READ detectedRows NOTIFY rowsChanged)
    Q_PROPERTY(
        QString detectedEdition READ detectedEdition WRITE setDetectedEdition NOTIFY rowsChanged)
    Q_PROPERTY(int movedRows READ movedRows NOTIFY rowsChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(int cardScalePercent READ cardScalePercent WRITE setCardScalePercent NOTIFY
                   cardScalePercentChanged)
public:
    GalleryController(DatabaseUtil *database,
                      SelectionModel *selection,
                      DeckBuilderController *deck,
                      QObject *parent = nullptr);

    QVariantList detectedRows() const { return rows_; }
    QString detectedEdition() const { return edition_; }
    int movedRows() const { return hidden_.size(); }
    QString notice() const { return notice_; }
    void setDetectedEdition(const QString &edition);

    int cardScalePercent() const;
    void setCardScalePercent(int percent);

    Q_INVOKABLE bool transferDetected(bool move);
    Q_INVOKABLE bool exportDetected(const QString &format);
    Q_INVOKABLE void restoreMoved();
signals:
    void rowsChanged();
    void noticeChanged();
    void showDeckRequested();
    void cardScalePercentChanged();

private:
    void rebuild();
    void schedule();
    void setNotice(const QString &notice);
    QByteArray fingerprint(int row) const;

    DatabaseUtil *database_;
    SelectionModel *selection_;
    DeckBuilderController *deck_;
    QTimer rebuildTimer_;
    QString edition_, notice_;
    QVariantList rows_;
    QHash<int, QByteArray> hidden_;
    QSet<QString> requested_;
    QHash<QString, QVariantMap> dataCache_;
};
