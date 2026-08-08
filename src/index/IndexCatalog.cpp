#include "IndexCatalog.h"

#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QLocale>
#include <QUrl>

static const char* MANIFEST_URL =
    "https://huggingface.co/datasets/SamTheCoder777/ws-index/raw/main/manifest.json";

IndexCatalog::IndexCatalog(DatasetManager* cardListDbManager, DatasetManager* seriesListDbManager,
                           QObject* parent) :
    QAbstractListModel(parent), cardListDbManager_(cardListDbManager), seriesListDbManager_(seriesListDbManager)
{
    QDir().mkpath(installRoot());
}

QString IndexCatalog::installRoot() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/indexes";
}
QString IndexCatalog::dirFor(const QString& id) const { return installRoot() + "/" + id; }

QVariant IndexCatalog::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() >= rows_.size()) return {};
    const Row& r = rows_[idx.row()];
    switch (role) {
    case IdRole:               return r.id;
    case NameRole:             return r.name;
    case DescRole:             return r.desc;
    case VersionRole:          return r.version;
    case InstalledVersionRole: return r.installedVersion;
    case SizeTextRole:         return QLocale().formattedDataSize(r.totalSize);
    case ProgressRole:         return r.progress;
    case DownloadingRole:      return r.downloading;
    case StatusRole:
        if (r.installedVersion.isEmpty())        return (int)NotInstalled;
        if (r.installedVersion != r.version)     return (int)UpdateAvailable;
        return (int)Installed;
    }
    return {};
}

QHash<int, QByteArray> IndexCatalog::roleNames() const {
    return {{IdRole,"idStr"}, {NameRole,"name"}, {DescRole,"desc"},
            {VersionRole,"version"}, {InstalledVersionRole,"installedVersion"},
            {SizeTextRole,"sizeText"}, {StatusRole,"statusCode"},
            {ProgressRole,"progress"}, {DownloadingRole,"downloading"}};
}

void IndexCatalog::touchRow(int row) {
    if (row >= 0 && row < rows_.size())
        emit dataChanged(index(row), index(row));
}

// ── installed-state file: <appdata>/indexes/installed.json ─────────────────
void IndexCatalog::loadInstalledState() {
    QFile f(installRoot() + "/installed.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (Row& r : rows_) {
        // only count it as installed if the files are actually still on disk
        const QString v = o.value(r.id).toString();
        if (!v.isEmpty() && QFile::exists(dirFor(r.id) + "/index.faiss"))
            r.installedVersion = v;
        else
            r.installedVersion.clear();
    }
}

void IndexCatalog::saveInstalledState() {
    QJsonObject o;
    for (const Row& r : rows_)
        if (!r.installedVersion.isEmpty()) o.insert(r.id, r.installedVersion);
    QDir().mkpath(installRoot());
    QFile f(installRoot() + "/installed.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

// ── manifest ────────────────────────────────────────────────────────────────
void IndexCatalog::refresh() {
    if (reply_) return;
    setStatus("Checking for index updates…");

    QNetworkRequest req{QUrl(MANIFEST_URL)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    reply_ = nam_.get(req);
    connect(reply_, &QNetworkReply::finished, this, [this] {
        QByteArray body;
        QString err;
        if (reply_->error() == QNetworkReply::NoError) body = reply_->readAll();
        else err = reply_->errorString();
        reply_->deleteLater();
        reply_ = nullptr;

        if (!err.isEmpty()) {
            setStatus("Could not reach the index server: " + err);
            emit errorOccurred(err);
            return;
        }

        QJsonParseError pe;
        QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            setStatus("Manifest is not valid JSON.");
            emit errorOccurred(pe.errorString());
            return;
        }

        beginResetModel();
        rows_.clear();
        for (const QJsonValue& v : doc.object().value("indexes").toArray()) {
            QJsonObject o = v.toObject();
            Row r;
            r.id      = o.value("id").toString();
            r.name    = o.value("name").toString(r.id);
            r.desc    = o.value("description").toString();
            r.version = o.value("version").toVariant().toString();
            for (const QJsonValue& fv : o.value("files").toArray()) {
                QJsonObject fo = fv.toObject();
                FileEntry fe;
                fe.name   = fo.value("name").toString();
                fe.url    = fo.value("url").toString();
                fe.sha256 = fo.value("sha256").toString();
                fe.size   = (qint64)fo.value("size").toDouble();
                r.totalSize += fe.size;
                r.files.push_back(fe);
            }
            if (!r.id.isEmpty() && !r.files.isEmpty()) rows_.push_back(r);
        }
        loadInstalledState();
        endResetModel();

        int updates = 0;
        for (const Row& r : rows_)
            if (!r.installedVersion.isEmpty() && r.installedVersion != r.version) ++updates;
        setStatus(updates > 0
            ? QString("%1 index(es) available · %2 update(s) ready").arg(rows_.size()).arg(updates)
            : QString("%1 index(es) available").arg(rows_.size()));

        emit refreshFinished();
    });
}

void IndexCatalog::useById(const QString& id) {
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == id) { use(i); return; }
}

// ── download (files fetched one after another) ─────────────────────────────
void IndexCatalog::download(int row) {
    if (reply_ || row < 0 || row >= rows_.size()) return;
    dlRow_  = row;
    dlFile_ = 0;
    dlBytesBefore_ = 0;
    dlDir_  = dirFor(rows_[row].id);
    QDir().mkpath(dlDir_);

    rows_[row].downloading = true;
    rows_[row].progress    = 0.0;
    touchRow(row);
    setStatus("Downloading " + rows_[row].name + "…");
    startNextFile();
}

void IndexCatalog::startNextFile() {
    if (dlRow_ < 0 || dlRow_ >= rows_.size()) return;
    Row& r = rows_[dlRow_];

    if (dlFile_ >= r.files.size()) {              // all files done
        r.installedVersion = r.version;
        saveInstalledState();
        finishDownload(true, r.name + " installed (v" + r.version + ").");
        return;
    }

    const FileEntry& fe = r.files[dlFile_];
    outFile_.setFileName(dlDir_ + "/" + fe.name + ".part");
    if (!outFile_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finishDownload(false, "Cannot write to " + dlDir_);
        return;
    }

    QNetworkRequest req{QUrl(fe.url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    reply_ = nam_.get(req);
    emit stateChanged();

    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (reply_) outFile_.write(reply_->readAll());
    });
    connect(reply_, &QNetworkReply::downloadProgress, this,
            [this](qint64 got, qint64 total) {
        if (dlRow_ < 0) return;
        Row& r = rows_[dlRow_];
        qint64 denom = r.totalSize > 0 ? r.totalSize
                                       : (dlBytesBefore_ + std::max<qint64>(total, 1));
        r.progress = double(dlBytesBefore_ + got) / double(denom);
        if (r.progress > 1.0) r.progress = 1.0;
        touchRow(dlRow_);
    });
    connect(reply_, &QNetworkReply::finished, this, [this] {
        const bool aborted = (reply_->error() == QNetworkReply::OperationCanceledError);
        const bool ok      = (reply_->error() == QNetworkReply::NoError);
        const QString err  = reply_->errorString();
        outFile_.write(reply_->readAll());
        outFile_.close();
        reply_->deleteLater();
        reply_ = nullptr;

        if (aborted) { QFile::remove(outFile_.fileName()); finishDownload(false, "Download cancelled."); return; }
        if (!ok)     { QFile::remove(outFile_.fileName()); finishDownload(false, "Download failed: " + err); return; }

        Row& r = rows_[dlRow_];
        const FileEntry& fe = r.files[dlFile_];

        // optional integrity check
        if (!fe.sha256.isEmpty()) {
            QFile f(outFile_.fileName());
            if (f.open(QIODevice::ReadOnly)) {
                QCryptographicHash h(QCryptographicHash::Sha256);
                h.addData(&f);
                if (h.result().toHex() != fe.sha256.toLatin1().toLower()) {
                    f.close();
                    QFile::remove(outFile_.fileName());
                    finishDownload(false, fe.name + " failed its checksum — download aborted.");
                    return;
                }
            }
        }

        const QString finalPath = dlDir_ + "/" + fe.name;
        QFile::remove(finalPath);
        QFile::rename(outFile_.fileName(), finalPath);

        dlBytesBefore_ += fe.size > 0 ? fe.size : QFileInfo(finalPath).size();
        ++dlFile_;
        startNextFile();
    });
}

void IndexCatalog::finishDownload(bool ok, const QString& message) {
    if (dlRow_ >= 0 && dlRow_ < rows_.size()) {
        rows_[dlRow_].downloading = false;
        rows_[dlRow_].progress    = ok ? 1.0 : 0.0;
        touchRow(dlRow_);
    }
    dlRow_ = -1;
    setStatus(message);
    if (!ok) emit errorOccurred(message);
}

void IndexCatalog::cancel() {
    if (reply_) reply_->abort();
}

void IndexCatalog::use(int row) {
    if (row < 0 || row >= rows_.size()) return;
    if (rows_[row].installedVersion.isEmpty()) return;
    activeId_ = rows_[row].id;
    emit useIndexRequested(dirFor(rows_[row].id));
    setStatus("Using " + rows_[row].name + ".");
}

void IndexCatalog::removeIndex(int row) {
    if (row < 0 || row >= rows_.size()) return;
    QDir(dirFor(rows_[row].id)).removeRecursively();
    rows_[row].installedVersion.clear();
    rows_[row].progress = 0.0;
    saveInstalledState();
    touchRow(row);
    setStatus(rows_[row].name + " removed.");
}
