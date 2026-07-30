#include "Models.h"
#include "database/DatabaseUtil.h"

#include <QSqlQuery>
#include <QUrl>
#include <QtSql/qsqlerror.h>

// "bd_w125_021" -> "BD/W125-021" (series prefix fixed to BD for now)
QString toDeckCode(const std::string& cardId) {
    QString s = QString::fromStdString(cardId).trimmed();
    QStringList parts = s.split('_', Qt::SkipEmptyParts);
    if (!parts.isEmpty() && parts.first().compare("bd", Qt::CaseInsensitive) == 0)
        parts.removeFirst();
    if (parts.size() >= 2) {
        QString set = parts.takeFirst().toUpper();
        return QString("BD/%1-%2").arg(set, parts.join('-').toUpper());
    }

    return s; // TODO already has CODE/card

    return "BD/" + s.toUpper();
}

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
        r.deckCode  = toDeckCode(x.card_id);
        r.score     = x.score;
        r.masterUrl = DatabaseUtil::imageUrlFor(r.cardId);
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
    }
    return {};
}

QHash<int, QByteArray> SelectionModel::roleNames() const {
    return {{LabelRole, "label"}, {ConfirmedRole, "confirmed"},
            {QtyRole, "qty"}, {NumberRole, "number"}};
}

void SelectionModel::setRows(const QVector<Row>& rows) {
    if (rows.size() != rows_.size()) {
        // count changed -> a real structural change, reset is correct
        beginResetModel();
        rows_ = rows;
        endResetModel();
    } else {
        // same count -> just update values, DON'T reset (preserves scroll)
        rows_ = rows;
        if (!rows_.isEmpty())
            emit dataChanged(index(0), index(rows_.size() - 1));
    }
}
