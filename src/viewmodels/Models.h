#pragma once

#include <QAbstractListModel>
#include <QImage>
#include <QQuickImageProvider>
#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include "../database/DatabaseUtil.h"
#include "../models/tcg_infer.h"

class CandidateModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int totalCount READ totalCount NOTIFY visibleChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY visibleChanged)
public:
    enum Roles {
        CardIdRole = Qt::UserRole + 1,
        DeckCodeRole,
        ScoreRole,
        MasterUrlRole,
        IsConfirmedRole,
        RankRole
    };
    static constexpr int kPageSize = 15;
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex & = {}) const override { return visible_; }
    QVariant data(const QModelIndex &idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int totalCount() const { return rows_.size(); }
    bool canLoadMore() const { return visible_ < rows_.size(); }
    Q_INVOKABLE void loadMore();

    void setCandidates(const std::vector<Candidate> &c, const std::string &confirmedId);
    void setConfirmedId(const std::string &id);
    void clear();

    void setCardDatabase(QSqlDatabase &db) { db_ = db; }
    void setDatabaseUtil(DatabaseUtil *dbUtil) { dbUtil_ = dbUtil; }

signals:
    void visibleChanged();

private:
    struct Row
    {
        QString cardId, deckCode;
        double score = 0;
        bool confirmed = false;
        bool manual = false;
        mutable QString masterUrl;
        mutable bool urlResolved = false;
    };
    QVector<Row> rows_;
    int visible_ = 0;
    QSqlDatabase db_;
    DatabaseUtil *dbUtil_ = nullptr;
};

class SelectionModel : public QAbstractListModel {
    Q_OBJECT
public:
    Q_INVOKABLE QVariant dataAt(int row, const QString& roleName) const;
    Q_INVOKABLE int      rowCountQml() const { return rows_.size(); }

    enum Roles { LabelRole = Qt::UserRole + 1, ConfirmedRole, QtyRole, NumberRole, CardIdRole };
    using QAbstractListModel::QAbstractListModel;

    struct Row
    {
        QString label;
        bool confirmed = false;
        int qty = 1;
        QString cardId;
    };

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
