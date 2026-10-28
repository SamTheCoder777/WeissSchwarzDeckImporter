#include "SeriesCatalog.h"

#include <QTimer>
#include "../core/Config.h"

SeriesCatalog::SeriesCatalog(SeriesRepository *repo, QObject *parent)
    : QAbstractListModel(parent), repo_(repo)
{
    connect(repo_, &SeriesRepository::seriesListUpdated,
            this, &SeriesCatalog::reloadFromRepo);
    connect(repo_, &SeriesRepository::busyChanged, this, &SeriesCatalog::stateChanged);
    connect(repo_, &SeriesRepository::checkStateChanged, this, &SeriesCatalog::stateChanged);
    connect(repo_, &SeriesRepository::updateAllStateChanged, this, &SeriesCatalog::stateChanged);
    connect(repo_, &SeriesRepository::statusChanged, this,
            [this](const QString &) { emit stateChanged(); });
    connect(repo_, &SeriesRepository::errorOccurred, this, &SeriesCatalog::errorOccurred);
    connect(repo_, &SeriesRepository::cardProgress, this,
        [this](const QString &id, double progress) {
            const int row = rowForId(id);
            if (row < 0) return;
            rows_[row].downloading = progress < 1.0;
            rows_[row].progress = progress;
            touchRow(row);
        });
    connect(repo_, &SeriesRepository::cardDownloadFinished, this,
        [this](const QString &id, bool success) {
            const int row = rowForId(id);
            if (row < 0) return;
            rows_[row].downloading = false;
            rows_[row].progress = success ? 1.0 : 0.0;
            touchRow(row);
        });
    connect(repo_, &SeriesRepository::cardListDownloaded, this,
        [this](const QString &id) {
            const int row = rowForId(id);
            if (row >= 0) {
                rows_[row].hasCardList = true;
                rows_[row].downloading = false;
                rows_[row].progress = 1.0;
                touchRow(row);
            }
            emit cardListDownloaded(id);
        });
    connect(repo_, &SeriesRepository::cardListUpdateState, this,
        [this](const QString &id, bool outdated) {
            if (outdated) outdatedIds_.insert(id);
            else outdatedIds_.remove(id);
            const int row = rowForId(id);
            if (row >= 0) {
                rows_[row].updateAvailable = outdated;
                touchRow(row);
            }
            emit stateChanged();
        });
    connect(repo_, &SeriesRepository::cardListCheckFinished, this,
            [this](int) { emit stateChanged(); });
    connect(&Config::instance(), &Config::detectLocaleModeChanged,
            this, &SeriesCatalog::reloadFromRepo);
    reloadFromRepo();
    QTimer::singleShot(1500, this, [this] {
        if (repo_) repo_->checkAutomaticUpdates();
    });
}

void SeriesCatalog::reloadFromRepo()
{
    QVector<Row> loaded;
    outdatedIds_.clear();
    for (const auto &series : repo_->loadSeries()) {
        Row row;
        row.id = series.id;
        row.set = series.set;
        row.name = series.name;
        row.hash = series.hash;
        row.hasCardList = series.hasCardList;
        row.updateAvailable = series.updateAvailable;
        if (row.updateAvailable) outdatedIds_.insert(row.id);
        loaded.append(row);
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
    case StatusRole:
        if (!r.hasCardList)
            return (int) NotDownloaded;
        return r.updateAvailable ? (int) UpdateAvailable : (int) Downloaded;
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
