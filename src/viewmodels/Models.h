// Models.h — QAbstractListModels that feed the QML panel, plus the crop image provider.
#pragma once

#include <QAbstractListModel>
#include <QQuickImageProvider>
#include <QVector>
#include <QString>
#include <QImage>
#include <QSqlDatabase>
#include "../database/DatabaseUtil.h"
#include "../retrieval/tcg_infer.h"

// ── top-15 candidates for the currently selected card ───────────────────────
class CandidateModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { CardIdRole = Qt::UserRole + 1, DeckCodeRole, ScoreRole,
                 MasterUrlRole, IsConfirmedRole, RankRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex& = {}) const override { return rows_.size(); }
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setCandidates(const std::vector<Candidate>& c, const std::string& confirmedId);
    void setConfirmedId(const std::string& id);
    void clear();

    void setCardDatabase(QSqlDatabase& db) { db_ = db; }
    void setDatabaseUtil(DatabaseUtil* dbUtil) {dbUtil_ = dbUtil;}

private:
    struct Row { QString cardId, deckCode, masterUrl; double score = 0; bool confirmed = false; };
    QVector<Row> rows_;
    QSqlDatabase db_;

    DatabaseUtil* dbUtil_ = nullptr;

    QString imageUrlFor(QString cardId);
};

// ── the list of card selections on the image ────────────────────────────────
class SelectionModel : public QAbstractListModel {
    Q_OBJECT
public:
    Q_INVOKABLE QVariant dataAt(int row, const QString& roleName) const;
    Q_INVOKABLE int      rowCountQml() const { return rows_.size(); }

    enum Roles { LabelRole = Qt::UserRole + 1, ConfirmedRole, QtyRole, NumberRole, CardIdRole };
    using QAbstractListModel::QAbstractListModel;

    struct Row { QString label; bool confirmed = false; int qty = 1; QString cardId;};

    int rowCount(const QModelIndex& = {}) const override { return rows_.size(); }
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const QVector<Row>& rows);

private:
    QVector<Row> rows_;
};

// ── serves the current crop to QML as image://crop/current ──────────────────
class CropImageProvider : public QQuickImageProvider {
public:
    CropImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString&, QSize* size, const QSize&) override {
        if (size) *size = img_.size();
        return img_;
    }
    void setImage(const QImage& i) { img_ = i; }
private:
    QImage img_;
};
