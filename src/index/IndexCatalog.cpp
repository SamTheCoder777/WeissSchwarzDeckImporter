#include "IndexCatalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "../core/Config.h"

static bool isEnMode()
{
    return Config::instance().getCurDetectLocaleMode() == Config::DetectLocaleMode::EN;
}
static const QString kEnPrefix = QStringLiteral("EN_");

IndexCatalog::IndexCatalog(QObject *parent)
    : QAbstractListModel(parent)
{
    QDir().mkpath(installRoot());

    connect(&Config::instance(),
            &Config::detectLocaleModeChanged,
            this,
            &IndexCatalog::onLocaleModeChanged);
}

void IndexCatalog::onLocaleModeChanged()
{
    ++refreshGen_;
    activeId_.clear();

    beginResetModel();
    rows_.clear();
    endResetModel();
    setStatus("Loading indexes…");

    if (reply_) {
        pendingRefresh_ = true;
        reply_->abort();
        return;
    }
    refresh();
}

QString IndexCatalog::installedJsonPath() const
{
    return installRoot() + (isEnMode() ? "/installed_en.json" : "/installed.json");
}

QJsonObject IndexCatalog::readInstalledJson() const
{
    QJsonObject o = readJsonFile(installedJsonPath());
    const QJsonObject custom = readJsonFile(customJsonPath());
    for (auto it = custom.begin(); it != custom.end(); ++it) {
        QJsonObject e = it.value().toObject();
        e.insert("custom", true);
        o.insert(it.key(), e);
    }
    return o;
}

void IndexCatalog::migrateInstalledJson()
{
    if (!isEnMode() || QFile::exists(installedJsonPath()))
        return;
    QFile f(installRoot() + "/installed.json");
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject legacy = QJsonDocument::fromJson(f.readAll()).object();

    QJsonObject en;
    for (auto it = legacy.begin(); it != legacy.end(); ++it)
        if (QFile::exists(installRoot() + "/" + kEnPrefix + it.key() + "/index.faiss"))
            en.insert(it.key(), it.value());
    if (en.isEmpty())
        return;

    QFile out(installedJsonPath());
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(QJsonDocument(en).toJson(QJsonDocument::Indented));
}

QString IndexCatalog::installRoot() const
{
    return Config::instance().getIndexInstallPath();
}

QString IndexCatalog::dirFor(const QString &id) const
{
    return installRoot() + "/" + (isEnMode() ? kEnPrefix + id : id);
}

QString IndexCatalog::customJsonPath() const
{
    return installRoot() + "/custom_indexes.json";
}

QJsonObject IndexCatalog::readJsonFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void IndexCatalog::writeJsonFile(const QString &path, const QJsonObject &o)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

QString IndexCatalog::dirForRow(const Row &r) const
{
    return r.isCustom ? installRoot() + "/" + r.id : dirFor(r.id);
}

void IndexCatalog::migrateCustomEntries()
{
    if (QFile::exists(customJsonPath()))
        return;
    const QJsonObject jp = readJsonFile(installRoot() + "/installed.json");
    QJsonObject custom;
    for (auto it = jp.begin(); it != jp.end(); ++it) {
        const QJsonValue v = it.value();
        const bool isCustom = v.isObject() ? v.toObject().value("custom").toBool(false)
                                           : (v.toString() == "local");
        if (isCustom && QFile::exists(installRoot() + "/" + it.key() + "/index.faiss"))
            custom.insert(it.key(), QJsonObject{{"version", "local"}});
    }
    QDir().mkpath(installRoot());
    writeJsonFile(customJsonPath(), custom); // written even if empty, so this runs once
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

        if (!v.isEmpty() && QFile::exists(dirForRow(r) + "/index.faiss")) {
            r.installedVersion = v;
            r.isCustom = storedCustom;
        } else if (!r.isCustom) {
            r.installedVersion.clear();
        }
    }
}

void IndexCatalog::saveInstalledState()
{
    QJsonObject cloud;

    QJsonObject custom = readJsonFile(customJsonPath());

    for (const Row &r : rows_) {
        if (r.isCustom) {
            if (r.installedVersion.isEmpty())
                custom.remove(r.id);
            else
                custom.insert(r.id, QJsonObject{{"version", r.installedVersion}});
            continue;
        }
        if (r.installedVersion.isEmpty())
            continue;
        cloud.insert(r.id, QJsonObject{{"version", r.installedVersion}, {"custom", false}});
    }

    for (const QString &k : custom.keys())
        if (!QFile::exists(installRoot() + "/" + k + "/index.faiss"))
            custom.remove(k);

    QDir().mkpath(installRoot());
    writeJsonFile(installedJsonPath(), cloud);
    writeJsonFile(customJsonPath(), custom);
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
    pendingRefresh_ = false;
    const quint64 gen = ++refreshGen_;
    setStatus("Checking for index updates…");

    migrateCustomEntries();
    migrateInstalledJson();

    beginResetModel();
    rows_.clear();
    scanLocalIndexes();
    loadInstalledState();
    pruneMissingCustomRows();
    endResetModel();

    const QString url = Config::instance().getIndexManifestUrl();
    qDebug() << "[IndexCatalog] refresh gen" << gen << "mode" << (isEnMode() ? "EN" : "JP")
             << "manifest:" << url;

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "WSDeckImporter/1.0");

    QNetworkReply *reply = nam_.get(req);
    reply_ = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen] {
        reply->deleteLater();
        if (reply_ == reply)
            reply_ = nullptr;

        if (pendingRefresh_) {
            pendingRefresh_ = false;
            QTimer::singleShot(0, this, &IndexCatalog::refresh);
            return;
        }

        if (gen != refreshGen_)
            return;

        QByteArray body;
        QString err;
        if (reply->error() == QNetworkReply::NoError)
            body = reply->readAll();
        else
            err = reply->errorString();

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

        rows_.clear();
        scanLocalIndexes();
        loadInstalledState();

        bool adopted = false;
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
                if (target->noEntry) {
                    target->isCustom = false;
                    target->installedVersion = "unknown";
                    adopted = true;
                } else {
                    setStatus("Custom index '" + id + "' shadows a cloud index of the same name.");
                    continue;
                }
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
        if (adopted)
            saveInstalledState();
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
    req.setHeader(QNetworkRequest::UserAgentHeader, "WSDeckImporter/1.0");
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
    if (!ok && !pendingRefresh_)
        emit errorOccurred(message);

    if (pendingRefresh_) {
        pendingRefresh_ = false;
        QTimer::singleShot(0, this, &IndexCatalog::refresh);
    }
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

    const QString dir = dirForRow(rows_[row]);

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
    QDir(dirForRow(rows_[row])).removeRecursively();
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

        const bool folderGone = !QFile::exists(dirForRow(r) + "/index.faiss");
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
    const bool en = isEnMode();
    const QJsonObject installed = readInstalledJson();
    const QJsonObject customs = readJsonFile(customJsonPath());
    const QJsonObject jpFile = en ? readJsonFile(installRoot() + "/installed.json") : QJsonObject();

    QDir root(installRoot());
    QVector<Row> disk;
    const QStringList subdirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &folder : subdirs) {
        const bool enFolder = folder.startsWith(kEnPrefix);
        if (enFolder && !en)
            continue;
        const QString id = enFolder ? folder.mid(kEnPrefix.size()) : folder;
        if (id.isEmpty())
            continue;

        QDir d(root.filePath(folder));
        const bool looksLikeIndex = d.exists("index.faiss") || d.exists("id_map.json")
                                    || d.exists("row2card.npy");
        if (!looksLikeIndex)
            continue;

        Row r;
        r.id = id;
        r.name = id;
        r.desc = "Local index";
        r.installedVersion = "local";

        if (!enFolder && en) {
            const QJsonValue jp = jpFile.value(id);
            const bool jpCloud = jp.isObject() && !jp.toObject().value("custom").toBool(true);
            if (!customs.contains(id) && jpCloud)
                continue;
            r.isCustom = true;
            r.noEntry = false;
        } else {
            const QJsonValue entry = installed.value(id);
            r.noEntry = !entry.isObject() && !entry.isString();
            const bool knownAsCatalog = entry.isObject()
                                        && !entry.toObject().value("custom").toBool(true);
            r.isCustom = !knownAsCatalog;
        }

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
