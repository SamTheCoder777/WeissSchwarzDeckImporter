#include "SeriesCatalog.h"

SeriesCatalog::SeriesCatalog(SeriesRepository* repo, QObject* parent)
    : QAbstractListModel(parent), repo_(repo) {

    connect(repo_, &SeriesRepository::seriesListUpdated,
            this, &SeriesCatalog::reloadFromRepo);

    connect(repo_, &SeriesRepository::busyChanged,   this, &SeriesCatalog::stateChanged);
    connect(repo_, &SeriesRepository::statusChanged,  this, [this](const QString&){ emit stateChanged(); });
    connect(repo_, &SeriesRepository::errorOccurred,  this, &SeriesCatalog::errorOccurred);

    connect(repo_, &SeriesRepository::cardProgress, this,
            [this](const QString& id, double p) {
        int r = rowForId(id);
        if (r < 0) return;
        rows_[r].downloading = (p < 1.0);
        rows_[r].progress = p;
        touchRow(r);
    });

    connect(repo_, &SeriesRepository::cardListDownloaded, this,
            [this](const QString& id) {
        int r = rowForId(id);
        if (r >= 0) { rows_[r].hasCardList = true; rows_[r].downloading = false;
                      rows_[r].progress = 1.0; touchRow(r); }
        emit cardListDownloaded(id);
    });

    reloadFromRepo();
}

void SeriesCatalog::reloadFromRepo() {
    QVector<Row> loaded;
    for (const SeriesRepository::SeriesRow& s : repo_->loadSeries()) {
        Row r;
        r.id = s.id; r.set = s.set; r.name = s.name; r.hash = s.hash;
        r.hasCardList = s.hasCardList;
        loaded.push_back(r);
    }
    beginResetModel();
    rows_ = loaded;
    endResetModel();
    emit stateChanged();
}

QVariant SeriesCatalog::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() >= rows_.size()) return {};
    const Row& r = rows_[idx.row()];
    switch (role) {
    case IdRole:          return r.id;
    case SetRole:         return r.set;
    case NameRole:        return r.name;
    case HashRole:        return r.hash;
    case StatusRole:      return r.hasCardList ? (int)Downloaded : (int)NotDownloaded;
    case DownloadingRole: return r.downloading;
    case ProgressRole:    return r.progress;
    }
    return {};
}

QHash<int, QByteArray> SeriesCatalog::roleNames() const {
    return {{IdRole,"idStr"}, {SetRole,"setCode"}, {NameRole,"name"},
            {HashRole,"hash"}, {StatusRole,"statusCode"},
            {DownloadingRole,"downloading"}, {ProgressRole,"progress"}};
}

int SeriesCatalog::rowForId(const QString& id) const {
    for (int i = 0; i < rows_.size(); ++i) if (rows_[i].id == id) return i;
    return -1;
}

void SeriesCatalog::touchRow(int row) {
    if (row >= 0 && row < rows_.size())
        emit dataChanged(index(row), index(row));
}
