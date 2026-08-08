#pragma once
#include <QSortFilterProxyModel>
#include "CardsCatalog.h"

class CardsSearchProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    // called from QML as the user types
    Q_INVOKABLE void setSearch(const QString& text) {
        if (search_ == text) return;
        search_ = text;
        invalidateFilter();          // re-run filterAcceptsRow for every row
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        if (search_.isEmpty()) return true;                 // no query -> show all
        QModelIndex i = sourceModel()->index(row, 0, parent);
        const QString name = sourceModel()->data(i, CardsCatalog::NameRole).toString();
        const QString desc = sourceModel()->data(i, CardsCatalog::DescRole).toString();
        return name.contains(search_, Qt::CaseInsensitive)
               || desc.contains(search_, Qt::CaseInsensitive);
    }

private:
    QString search_;
};