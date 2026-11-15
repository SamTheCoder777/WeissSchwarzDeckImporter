#pragma once
#include <QAbstractListModel>
#include <QImage>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <QWidget>

class DeckBuilderDeckModel final : public QAbstractListModel
{
public:
    struct Entry
    {
        QVariantMap card;
        int quantity = 1;
    };

    enum Role { CardRole = Qt::UserRole + 1, QuantityRole };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int quantity(const QString &code) const;
    void setQuantity(const QVariantMap &card, int quantity);

    void replace(QVector<Entry> entries);
    const QVector<Entry> &entries() const { return entries_; }

private:
    QVector<Entry> entries_;
};

class DeckBuilderController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList titles READ titles NOTIFY titlesChanged)
    Q_PROPERTY(QString catalogueNotice READ catalogueNotice NOTIFY titlesChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap selectedCard READ selectedCard NOTIFY selectedChanged)
    Q_PROPERTY(QAbstractItemModel *deckModel READ deckModel CONSTANT)
    Q_PROPERTY(QString edition READ edition NOTIFY stateChanged)
    Q_PROPERTY(QString language READ language NOTIFY stateChanged)
    Q_PROPERTY(QString deckName READ deckName WRITE setDeckName NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY stateChanged)
    Q_PROPERTY(bool exportCancellable READ exportCancellable NOTIFY stateChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY deckChanged)
    Q_PROPERTY(int totalMatches READ totalMatches NOTIFY stateChanged)
    Q_PROPERTY(int page READ page NOTIFY stateChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY stateChanged)
    Q_PROPERTY(bool loadingMore READ loadingMore NOTIFY stateChanged)
    Q_PROPERTY(bool editionChangePending READ editionChangePending NOTIFY stateChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY stateChanged)
    Q_PROPERTY(QVariantList referenceResults READ referenceResults NOTIFY referenceChanged)
    Q_PROPERTY(int referenceTotal READ referenceTotal NOTIFY referenceChanged)
    Q_PROPERTY(bool referenceBusy READ referenceBusy NOTIFY referenceChanged)
    Q_PROPERTY(bool referenceHasMore READ referenceHasMore NOTIFY referenceChanged)
    Q_PROPERTY(QString referenceError READ referenceError NOTIFY referenceChanged)
    Q_PROPERTY(QString referenceKind READ referenceKind NOTIFY referenceChanged)
    Q_PROPERTY(QString referenceValue READ referenceValue NOTIFY referenceChanged)
    Q_PROPERTY(bool referenceDeckOnly READ referenceDeckOnly WRITE setReferenceDeckOnly NOTIFY
                   referenceChanged)
    Q_PROPERTY(int totalCards READ totalCards NOTIFY deckChanged)
    Q_PROPERTY(int distinctCards READ distinctCards NOTIFY deckChanged)
    Q_PROPERTY(int climaxCards READ climaxCards NOTIFY deckChanged)
    Q_PROPERTY(int deckRevision READ deckRevision NOTIFY deckChanged)
    Q_PROPERTY(QVariantMap deckStats READ deckStats NOTIFY deckChanged)
    Q_PROPERTY(QVariantList deckRows READ deckRows NOTIFY deckChanged)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY deckChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY deckChanged)
public:
    explicit DeckBuilderController(QString path, QWidget *dialogParent);
    ~DeckBuilderController() override;
    QVariantList titles() const { return titles_; }
    QString catalogueNotice() const { return catalogueNotice_; }
    QVariantList results() const { return results_; }
    QVariantMap selectedCard() const { return selected_; }
    QAbstractItemModel *deckModel() { return &deck_; }

    QString edition() const { return edition_; }
    QString language() const { return language_; }
    QString deckName() const { return name_; }
    QString status() const { return status_; }

    bool busy() const { return searchRunning_ || titlesRunning_; }
    bool exporting() const { return exporting_; }
    bool exportCancellable() const { return exporting_ && !writingExport_; }
    bool dirty() const { return dirty_; }
    int totalMatches() const { return totalMatches_; }
    int page() const { return page_; }
    bool hasMore() const { return results_.size() < totalMatches_; }
    bool loadingMore() const { return loadingMore_; }
    QString loadError() const { return loadError_; }

    QVariantList referenceResults() const { return referenceResults_; }
    int referenceTotal() const { return referenceTotal_; }
    bool referenceBusy() const { return referenceRunning_; }
    bool referenceHasMore() const { return referenceResults_.size() < referenceTotal_; }
    QString referenceError() const { return referenceError_; }
    QString referenceKind() const { return referenceKind_; }
    QString referenceValue() const { return referenceValue_; }
    bool referenceDeckOnly() const { return referenceDeckOnly_; }
    void setReferenceDeckOnly(bool enabled);

    int totalCards() const;
    int distinctCards() const { return deck_.rowCount(); }
    int climaxCards() const;

    int deckRevision() const { return deckRevision_; }
    QVariantMap deckStats() const { return deckStats_; }
    void setDeckName(const QString &name);

    bool editionChangePending() const { return editionChangePending_; }
    Q_INVOKABLE void requestEdition(const QString &edition);

    Q_INVOKABLE bool setEdition(const QString &edition);
    Q_INVOKABLE void setLanguage(const QString &language);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void search(const QString &titleNumber, const QVariantMap &filters, int page = 0);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void openReference(const QString &kind, const QString &value);
    Q_INVOKABLE void setReferenceSort(const QString &group, const QString &sort, bool descending);
    Q_INVOKABLE void closeReference();
    Q_INVOKABLE void loadMoreReferences();
    Q_INVOKABLE void selectCard(const QVariantMap &card);
    Q_INVOKABLE void addCard(const QVariantMap &card);
    Q_INVOKABLE void changeQuantity(const QVariantMap &card, int delta);
    Q_INVOKABLE int quantity(const QString &code) const;
    Q_INVOKABLE QString imageSource(const QString &picture) const;

    Q_INVOKABLE bool saveText();
    Q_INVOKABLE void openText();
    Q_INVOKABLE void newDeck();
    Q_INVOKABLE void renameDeck();
    Q_INVOKABLE void exportImage();
    Q_INVOKABLE void copyDeckCode();
    Q_INVOKABLE void importDeckCode();
    Q_INVOKABLE void cancelExport();

    QVariantList deckRows() const;
    Q_INVOKABLE bool replaceCards(const QVariantList &rows, const QString &edition);
    Q_INVOKABLE bool exportSnapshot(const QVariantList &rows,
                                    const QString &edition,
                                    const QString &name,
                                    const QString &format);
    Q_INVOKABLE void showInGallery() { emit galleryRequested(); }
    bool confirmDiscard();

    bool canUndo() const { return !undoEdits_.isEmpty(); }
    bool canRedo() const { return !redoEdits_.isEmpty(); }
    Q_INVOKABLE void undoDeckEdit();
    Q_INVOKABLE void redoDeckEdit();

signals:
    void galleryRequested();
    void titlesChanged();
    void resultsChanged();
    void selectedChanged();
    void resultsReset();
    void referenceChanged();
    void referenceReset();
    void stateChanged();
    void deckChanged();

private:
    void dispatchSearch();
    void dispatchReference();
    void changedDeck(bool dirty = true);
    void setStatus(const QString &status);
    void nextExportImage();
    void renderExport();
    bool copyCodeEntries(const QVector<DeckBuilderDeckModel::Entry> &entries);
    bool saveEntries(const QVector<DeckBuilderDeckModel::Entry> &entries,
                     const QString &edition,
                     const QString &name);
    bool exportEntries(const QVector<DeckBuilderDeckModel::Entry> &entries, const QString &name);
    bool confirmReplaceDeck(const QString &title, const QString &message, const QString &actionText);

    QString path_, edition_ = "jp", language_ = "EN", name_ = "My Deck", status_;
    QString title_, catalogueNotice_;
    QStringList referenceSetCodes_, referenceDeckCodes_;
    QString referenceGroup_ = "none", referenceSort_ = "code";
    bool referenceDescending_ = false;
    bool referenceDeckOnly_ = false, referenceOpen_ = false;

    QVariantMap filters_, selected_, deckStats_;
    QVariantList titles_, results_;
    DeckBuilderDeckModel deck_;

    QWidget *dialogParent_;

    bool dirty_ = false, searchRunning_ = false, searchPending_ = false;
    bool titlesRunning_ = false;
    bool pendingAppend_ = false, loadingMore_ = false;
    QString loadError_;
    QVariantList referenceResults_;
    QString referenceKind_, referenceValue_, referenceTitle_, referenceError_;
    int referenceTotal_ = 0;
    bool referenceRunning_ = false, referencePending_ = false, referenceAppend_ = false;
    quint64 referenceGeneration_ = 0;
    int totalMatches_ = 0, page_ = 0, deckRevision_ = 0;
    quint64 generation_ = 0, titlesGeneration_ = 0;
    bool editionChangePending_ = false;

    bool exporting_ = false, writingExport_ = false;
    quint64 exportGeneration_ = 0;
    quint64 exportRequest_ = 0;
    int exportIndex_ = 0;
    QString exportPath_, exportName_;
    QStringList exportPngPaths_;
    QVector<DeckBuilderDeckModel::Entry> exportDeck_;
    QVector<QImage> exportImages_;

    struct MembershipEdit
    {
        QString code;
        QString edition;
        int before = 0;
        int after = 0;
    };
    QVector<MembershipEdit> undoEdits_;
    QVector<MembershipEdit> redoEdits_;
    void clearDeckHistory();
    bool applyMembershipEdit(const MembershipEdit &edit, bool undo);
};
