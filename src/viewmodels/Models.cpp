#include "Models.h"
#include "../database/DatabaseUtil.h"

#include <QSqlQuery>
#include <QUrl>
#include <QtSql/qsqlerror.h>



QVariant CandidateModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() >= rows_.size()) return {};
    const Row& r = rows_[idx.row()];
    switch (role) {
    case CardIdRole:      return r.cardId;
    case DeckCodeRole:    return r.deckCode;
    case ScoreRole:       return r.score;
    case MasterUrlRole:   return r.masterUrl;
    case IsConfirmedRole: return r.confirmed;
    case RankRole:        return idx.row() + 1;
    }
    return {};
}

QHash<int, QByteArray> CandidateModel::roleNames() const {
    return {{CardIdRole, "cardId"}, {DeckCodeRole, "deckCode"}, {ScoreRole, "score"},
            {MasterUrlRole, "masterUrl"}, {IsConfirmedRole, "isConfirmed"}, {RankRole, "rank"}};
}

void CandidateModel::setCandidates(const std::vector<Candidate>& c, const std::string& confirmedId) {
    beginResetModel();
    rows_.clear();
    for (const auto& x : c) {
        Row r;
        r.cardId    = QString::fromStdString(x.card_id);
        r.deckCode  = QString::fromStdString(x.card_id);
        r.score     = x.score;
        r.masterUrl = dbUtil_->imageUrlFor(r.cardId);
        r.confirmed = (!confirmedId.empty() && confirmedId == x.card_id);
        rows_.push_back(r);
    }
    endResetModel();
}

void CandidateModel::setConfirmedId(const std::string& id) {
    for (int i = 0; i < rows_.size(); ++i)
        rows_[i].confirmed = (!id.empty() && rows_[i].cardId == QString::fromStdString(id));
    if (!rows_.isEmpty())
        emit dataChanged(index(0), index(rows_.size() - 1), {IsConfirmedRole});
}

void CandidateModel::clear() { beginResetModel(); rows_.clear(); endResetModel(); }

QVariant SelectionModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() >= rows_.size()) return {};
    const Row& r = rows_[idx.row()];
    switch (role) {
    case LabelRole:     return r.label;
    case ConfirmedRole: return r.confirmed;
    case QtyRole:       return r.qty;
    case NumberRole:    return idx.row() + 1;
    case CardIdRole: return r.cardId;
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
