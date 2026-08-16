#pragma once
#include <QSortFilterProxyModel>
#include "SeriesCatalog.h"

class SeriesSearchProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    Q_INVOKABLE void setSearch(const QString& text) {
        if (search_ == text) return;
        search_ = text;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        if (search_.isEmpty()) return true;
        QModelIndex i = sourceModel()->index(row, 0, parent);
        const QString name = sourceModel()->data(i, SeriesCatalog::NameRole).toString();
        const QString set  = sourceModel()->data(i, SeriesCatalog::SetRole).toString();
        return name.contains(search_, Qt::CaseInsensitive)
               || set.contains(search_, Qt::CaseInsensitive);
    }

private:
    QString search_;
};