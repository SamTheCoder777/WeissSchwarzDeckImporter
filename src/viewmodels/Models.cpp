#include "Models.h"
#include "../database/DatabaseUtil.h"

#include <QSqlQuery>
#include <QUrl>
#include <QtSql/qsqlerror.h>

QVariant CandidateModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= visible_)
        return {};
    const Row &r = rows_[idx.row()];
    switch (role) {
    case CardIdRole:
        return r.cardId;
    case DeckCodeRole:
        return r.deckCode;
    case ScoreRole:
        return r.score;
    case MasterUrlRole:
        if (!r.urlResolved) {
            r.masterUrl = dbUtil_ ? dbUtil_->imageUrlFor(r.cardId) : QString();
            r.urlResolved = true;
        }
        return r.masterUrl;
    case IsConfirmedRole:
        return r.confirmed;
    case RankRole: {
        if (r.manual)
            return 0;
        int manualBefore = 0;
        for (int i = 0; i < idx.row(); ++i)
            if (rows_[i].manual)
                ++manualBefore;
        return idx.row() + 1 - manualBefore;
    }
    }
    return {};
}

QHash<int, QByteArray> CandidateModel::roleNames() const {
    return {{CardIdRole, "cardId"}, {DeckCodeRole, "deckCode"}, {ScoreRole, "score"},
            {MasterUrlRole, "masterUrl"}, {IsConfirmedRole, "isConfirmed"}, {RankRole, "rank"}};
}

void CandidateModel::setCandidates(const std::vector<Candidate> &c, const std::string &confirmedId)
{
    beginResetModel();
    rows_.clear();
    rows_.reserve(int(c.size()));
    int confirmedRow = -1;
    for (const auto &x : c) {
        Row r;
        r.cardId = QString::fromStdString(x.card_id);
        r.deckCode = r.cardId;
        r.score = x.score;
        r.manual = x.score < 0;
        r.confirmed = (!confirmedId.empty() && confirmedId == x.card_id);
        if (r.confirmed && confirmedRow < 0)
            confirmedRow = rows_.size();
        rows_.push_back(r);
    }

    visible_ = std::min<int>(rows_.size(), std::max(kPageSize, confirmedRow + 1));
    endResetModel();
    emit visibleChanged();
}

void CandidateModel::setConfirmedId(const std::string &id)
{
    const QString qid = QString::fromStdString(id);
    for (Row &r : rows_)
        r.confirmed = (!id.empty() && r.cardId == qid);
    if (visible_ > 0)
        emit dataChanged(index(0), index(visible_ - 1), {IsConfirmedRole});
}

void CandidateModel::clear()
{
    beginResetModel();
    rows_.clear();
    visible_ = 0;
    endResetModel();
    emit visibleChanged();
}

void CandidateModel::loadMore()
{
    if (!canLoadMore())
        return;
    const int next = std::min<int>(rows_.size(), visible_ + kPageSize);
    beginInsertRows(QModelIndex(), visible_, next - 1);
    visible_ = next;
    endInsertRows();
    emit visibleChanged();
}

QVariant SelectionModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= rows_.size())
        return {};
    const Row &r = rows_[idx.row()];
    switch (role) {
    case LabelRole:
        return r.label;
    case ConfirmedRole:
        return r.confirmed;
    case QtyRole:
        return r.qty;
    case NumberRole:
        return idx.row() + 1;
    case CardIdRole:
        return r.cardId;
    }
    return {};
}

QHash<int, QByteArray> SelectionModel::roleNames() const {
    return {{LabelRole, "label"}, {ConfirmedRole, "confirmed"},
            {QtyRole, "qty"}, {NumberRole, "number"}, {CardIdRole, "cardId"}};
}

void SelectionModel::setRows(const QVector<Row>& rows) {
    if (rows.size() != rows_.size()) {
        beginResetModel();
        rows_ = rows;
        endResetModel();
    } else {
        rows_ = rows;
        if (!rows_.isEmpty())
            emit dataChanged(index(0), index(rows_.size() - 1));
    }
}

QVariant SelectionModel::dataAt(int row, const QString& roleName) const {
    if (row < 0 || row >= rows_.size()) return {};
    const QByteArray rn = roleName.toUtf8();
    const auto names = roleNames();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it)
        if (it.value() == rn)
            return data(index(row), it.key());
    return {};
}
