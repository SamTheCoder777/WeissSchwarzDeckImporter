#include "IndexCatalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStandardPaths>
#include <QUrl>

#include "../core/Config.h"

IndexCatalog::IndexCatalog(QObject *parent)
    : QAbstractListModel(parent)
{
    QDir().mkpath(installRoot());
}

QJsonObject IndexCatalog::readInstalledJson() const
{
    QFile f(installRoot() + "/installed.json");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QString IndexCatalog::installRoot() const
{
    return Config::instance().getIndexInstallPath();
}
QString IndexCatalog::dirFor(const QString &id) const
{
    return installRoot() + "/" + id;
}

QVariant IndexCatalog::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= rows_.size())
        return {};
    const Row &r = rows_[idx.row()];
    switch (role) {
    case IdRole:
        return r.id;
    case NameRole:
        return r.name;
    case DescRole:
        return r.desc;
    case VersionRole:
        return r.version;
    case InstalledVersionRole:
        return r.installedVersion;
    case SizeTextRole:
        return QLocale().formattedDataSize(r.totalSize);
    case ProgressRole:
        return r.progress;
    case DownloadingRole:
        return r.downloading;
    case StatusRole:
        if (r.files.isEmpty() && !r.installedVersion.isEmpty())
            return (int) Installed; // local/custom, no remote: just Installed
        if (r.installedVersion.isEmpty())
            return (int) NotInstalled;
        if (!r.version.isEmpty() && r.installedVersion != r.version)
            return (int) UpdateAvailable;
        return (int) Installed;
    case HasRemoteRole:
        return !r.files.isEmpty();
    }
    return {};
}

QHash<int, QByteArray> IndexCatalog::roleNames() const
{
    return {{IdRole, "idStr"},
            {NameRole, "name"},
            {DescRole, "desc"},
            {VersionRole, "version"},
            {InstalledVersionRole, "installedVersion"},
            {SizeTextRole, "sizeText"},
            {StatusRole, "statusCode"},
            {ProgressRole, "progress"},
            {DownloadingRole, "downloading"},
            {HasRemoteRole, "hasRemote"}};
}

void IndexCatalog::touchRow(int row)
{
    if (row >= 0 && row < rows_.size())
        emit dataChanged(index(row), index(row));
}

void IndexCatalog::loadInstalledState()
{
    const QJsonObject o = readInstalledJson();
    for (Row &r : rows_) {
        const QJsonValue entry = o.value(r.id);
        QString v;
        bool storedCustom = r.isCustom;

        if (entry.isObject()) {
            const QJsonObject eo = entry.toObject();
            v = eo.value("version").toString();
            storedCustom = eo.value("custom").toBool(r.isCustom);
        } else if (entry.isString()) {
            v = entry.toString();
            storedCustom = (v == "local");
        }

        if (!v.isEmpty() && QFile::exists(dirFor(r.id) + "/index.faiss")) {
            r.installedVersion = v;
            r.isCustom = storedCustom;
        } else if (!r.isCustom) {
            r.installedVersion.clear();
        }
    }
}

void IndexCatalog::saveInstalledState()
{
    QJsonObject o;
    for (const Row &r : rows_){
        if (r.installedVersion.isEmpty())
            continue;
        QJsonObject entry;
        entry.insert("version", r.installedVersion);
        entry.insert("custom", r.isCustom);
        o.insert(r.id, entry);
    }
    QDir().mkpath(installRoot());
    QFile f(installRoot() + "/installed.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

int IndexCatalog::dlRowById()
{
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == dlId_)
            return i;
    return -1;
}

void IndexCatalog::refresh()
{
    if (reply_)
        return;
    setStatus("Checking for index updates…");

    beginResetModel();
    rows_.clear();
    scanLocalIndexes();
    loadInstalledState();
    pruneMissingCustomRows();
    endResetModel();

    QNetworkRequest req{QUrl(Config::instance().getIndexManifestUrl())};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    qDebug() << "[IndexCatalog] api call to " << Config::instance().getIndexManifestUrl();
    reply_ = nam_.get(req);
    connect(reply_, &QNetworkReply::finished, this, [this] {
        QByteArray body;
        QString err;
        if (reply_->error() == QNetworkReply::NoError)
            body = reply_->readAll();
        else
            err = reply_->errorString();
        reply_->deleteLater();
        reply_ = nullptr;

        if (!err.isEmpty()) {
            setStatus(QString("Offline · showing %1 local index(es)").arg(rows_.size()));
            emit refreshFinished();
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
        for (const QJsonValue &v : doc.object().value("indexes").toArray()) {
            QJsonObject o = v.toObject();
            const QString id = o.value("id").toString();
            if (id.isEmpty())
                continue;

            Row *target = nullptr;
            for (Row &r : rows_)
                if (r.id == id) {
                    target = &r;
                    break;
                }

            if (target && target->isCustom) {
                setStatus("Custom index '" + id + "' shadows a cloud index of the same name.");
                continue;
            }

            if (!target) {
                Row nr;
                nr.id = id;
                rows_.push_back(nr);
                target = &rows_.last();
            }

            target->name = o.value("name").toString(id);
            target->desc = o.value("description").toString(target->desc);
            target->version = o.value("version").toVariant().toString();
            target->files.clear();
            target->totalSize = 0;
            for (const QJsonValue &fv : o.value("files").toArray()) {
                QJsonObject fo = fv.toObject();
                FileEntry fe;
                fe.name = fo.value("name").toString();
                fe.url = fo.value("url").toString();
                fe.sha256 = fo.value("sha256").toString();
                fe.size = (qint64) fo.value("size").toDouble();
                target->totalSize += fe.size;
                target->files.push_back(fe);
            }
        }
        loadInstalledState();
        pruneMissingCustomRows();
        endResetModel();

        int updates = 0;
        for (const Row &r : rows_)
            if (!r.installedVersion.isEmpty() && r.installedVersion != r.version)
                ++updates;
        setStatus(updates > 0 ? QString("%1 index(es) available · %2 update(s) ready")
                                    .arg(rows_.size())
                                    .arg(updates)
                              : QString("%1 index(es) available").arg(rows_.size()));

        emit refreshFinished();
    });
}

void IndexCatalog::useById(const QString &id)
{
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == id) {
            use(i);
            return;
        }
}

void IndexCatalog::download(int row)
{
    if (reply_ || row < 0 || row >= rows_.size())
        return;
    dlRow_ = row;
    dlId_ = rows_[row].id;
    dlFile_ = 0;
    dlBytesBefore_ = 0;
    dlDir_ = dirFor(rows_[row].id);
    QDir().mkpath(dlDir_);

    rows_[row].downloading = true;
    rows_[row].progress = 0.0;
    touchRow(row);
    setStatus("Downloading " + rows_[row].name + "…");
    startNextFile();
}

void IndexCatalog::startNextFile()
{
    int row = dlRowById();
    if (row < 0)
        return;

    Row &r = rows_[row];

    if (dlFile_ >= r.files.size()) {
        r.installedVersion = r.version;
        saveInstalledState();
        finishDownload(true, r.name + " installed (v" + r.version + ").");
        return;
    }

    const FileEntry &fe = r.files[dlFile_];
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
        if (reply_)
            outFile_.write(reply_->readAll());
    });
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        int row = dlRowById();
        if (row < 0)
            return;
        Row &r = rows_[row];
        qint64 denom = r.totalSize > 0 ? r.totalSize
                                       : (dlBytesBefore_ + std::max<qint64>(total, 1));
        r.progress = double(dlBytesBefore_ + got) / double(denom);
        if (r.progress > 1.0)
            r.progress = 1.0;
        touchRow(row);
    });
    connect(reply_, &QNetworkReply::finished, this, [this] {
        const bool aborted = (reply_->error() == QNetworkReply::OperationCanceledError);
        const bool ok = (reply_->error() == QNetworkReply::NoError);
        const QString err = reply_->errorString();
        outFile_.write(reply_->readAll());
        outFile_.close();
        reply_->deleteLater();
        reply_ = nullptr;

        if (aborted) {
            QFile::remove(outFile_.fileName());
            finishDownload(false, "Download cancelled.");
            return;
        }
        if (!ok) {
            QFile::remove(outFile_.fileName());
            finishDownload(false, "Download failed: " + err);
            return;
        }

        int row = dlRowById();
        if (row < 0) {
            QFile::remove(outFile_.fileName());
            finishDownload(false, "Index no longer present.");
            return;
        }
        Row &r = rows_[row];
        if (dlFile_ >= r.files.size()) {
            finishDownload(false, "Index changed during download.");
            return;
        }
        const FileEntry &fe = r.files[dlFile_];

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

void IndexCatalog::finishDownload(bool ok, const QString &message)
{
    int row = dlRowById();
    if (row >= 0) {
        rows_[row].downloading = false;
        rows_[row].progress = ok ? 1.0 : 0.0;
        touchRow(row);
    }
    dlRow_ = -1;
    dlId_.clear();
    setStatus(message);
    if (!ok)
        emit errorOccurred(message);
}

void IndexCatalog::cancel()
{
    if (reply_)
        reply_->abort();
}

void IndexCatalog::use(int row)
{
    if (row < 0 || row >= rows_.size())
        return;

    const QString dir = dirFor(rows_[row].id);

    if (!QFile::exists(dir + "/index.faiss")) {
        setStatus("Index files not found in " + dir);
        emit errorOccurred("This index has no index.faiss on disk.");
        return;
    }

    activeId_ = rows_[row].id;
    emit useIndexRequested(dir);
    setStatus("Using " + rows_[row].name + ".");
    emit stateChanged();
}

void IndexCatalog::removeIndex(int row)
{
    if (row < 0 || row >= rows_.size())
        return;
    QDir(dirFor(rows_[row].id)).removeRecursively();
    rows_[row].installedVersion.clear();
    rows_[row].progress = 0.0;
    saveInstalledState();
    touchRow(row);
    setStatus(rows_[row].name + " removed.");
}

void IndexCatalog::pruneMissingCustomRows()
{
    bool changed = false;
    for (int i = rows_.size() - 1; i >= 0; --i) {
        Row &r = rows_[i];

        if (!r.isCustom)
            continue;
        if (r.downloading)
            continue;
        if (dlId_ == r.id)
            continue;

        const bool folderGone = !QFile::exists(dirFor(r.id) + "/index.faiss");
        if (folderGone) {
            rows_.remove(i);
            changed = true;
        }
    }
    if (changed)
        saveInstalledState();
}

void IndexCatalog::scanLocalIndexes()
{
    const QJsonObject installed = readInstalledJson();
    QDir root(installRoot());
    QVector<Row> disk;
    const QStringList subdirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &id : subdirs) {
        QDir d(root.filePath(id));
        // treat a folder as an index if it has the core files
        const bool looksLikeIndex = d.exists("index.faiss") || d.exists("id_map.json")
                                    || d.exists("row2card.npy");
        if (!looksLikeIndex)
            continue;

        Row r;
        r.id = id;
        r.name = id;
        r.desc = "Local index";
        r.installedVersion = "local";

        bool knownAsCatalog = false;
        const QJsonValue entry = installed.value(id);
        if (entry.isObject())
            knownAsCatalog = !entry.toObject().value("custom").toBool(true);
        r.isCustom = !knownAsCatalog;

        for (const QFileInfo &fi : d.entryInfoList(QDir::Files))
            r.totalSize += fi.size();
        disk.push_back(r);
    }

    for (const Row &dr : disk) {
        bool found = false;
        for (Row &r : rows_)
            if (r.id == dr.id) {
                found = true;
                break;
            }
        if (!found)
            rows_.push_back(dr);
    }
}
