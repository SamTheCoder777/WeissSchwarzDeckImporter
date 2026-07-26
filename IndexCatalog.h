// IndexCatalog.h — downloadable FAISS index catalogue.
//
// A remote manifest (JSON) lists the indexes you publish. The app compares each
// entry's version with what is installed locally and offers Download / Update /
// Use. Files are fetched individually (index.faiss, row2card.npy, id_map.json,
// engine_meta.json) so no zip library is needed.
//
// Manifest format:
// {
//   "indexes": [{
//      "id": "bd_336_v9",
//      "name": "BanG Dream! (336px)",
//      "version": "9",
//      "description": "3,052 cards · 79,352 vectors",
//      "files": [
//        {"name":"index.faiss",      "url":"https://…/index.faiss",      "size":81234567,
//         "sha256":"…"},
//        {"name":"row2card.npy",     "url":"https://…/row2card.npy",     "size":634816},
//        {"name":"id_map.json",      "url":"https://…/id_map.json",      "size":73210},
//        {"name":"engine_meta.json", "url":"https://…/engine_meta.json", "size":64}
//      ]
//   }]
// }
#pragma once

#include <QAbstractListModel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVector>
#include <QString>
#include <QFile>

class IndexCatalog : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString status   READ status   NOTIFY stateChanged)
    Q_PROPERTY(bool    busy     READ busy     NOTIFY stateChanged)
    Q_PROPERTY(QString installRoot READ installRoot CONSTANT)
    Q_PROPERTY(QString activeIndexId READ activeIndexId NOTIFY stateChanged)

public:
    enum Status { NotInstalled = 0, Installed = 1, UpdateAvailable = 2 };
    Q_ENUM(Status)

    enum Roles { IdRole = Qt::UserRole + 1, NameRole, DescRole, VersionRole,
                 InstalledVersionRole, SizeTextRole, StatusRole,
                 ProgressRole, DownloadingRole };

    explicit IndexCatalog(QObject* parent = nullptr);

    int rowCount(const QModelIndex& = {}) const override { return rows_.size(); }
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString status() const { return status_; }
    bool    busy()   const { return reply_ != nullptr; }
    QString installRoot() const;
    QString activeIndexId() const { return activeId_; }

    // called from QML
    Q_INVOKABLE void refresh();               // fetch the manifest
    Q_INVOKABLE void download(int row);       // download / update one index
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void use(int row);            // tell the app to use this index
    Q_INVOKABLE void removeIndex(int row);    // delete local files
    Q_INVOKABLE void useById(const QString& id);
    Q_INVOKABLE void downloadById(const QString& id) {
        for (int i = 0; i < rows_.size(); ++i)
            if (rows_[i].id == id) { download(i); return; }
    }
    Q_INVOKABLE void removeById(const QString& id) {
        for (int i = 0; i < rows_.size(); ++i)
            if (rows_[i].id == id) { removeIndex(i); return; }
    }

signals:
    void stateChanged();
    void useIndexRequested(const QString& localDir);   // -> MainWindow
    void errorOccurred(const QString& message);
    void refreshFinished();

private:
    struct FileEntry { QString name, url, sha256; qint64 size = 0; };
    struct Row {
        QString id, name, desc, version, installedVersion;
        QVector<FileEntry> files;
        qint64 totalSize = 0;
        double progress = 0.0;
        bool   downloading = false;
    };

    void setStatus(const QString& s) { qDebug() << "stateChanged! " + s; status_ = s; emit stateChanged(); }
    void loadInstalledState();
    void saveInstalledState();
    void startNextFile();
    void finishDownload(bool ok, const QString& message);
    QString dirFor(const QString& id) const;
    void touchRow(int row);

    QNetworkAccessManager nam_;
    QNetworkReply* reply_ = nullptr;
    QFile          outFile_;

    QVector<Row> rows_;
    QString status_ = "Ready.";

    // active download state
    int     dlRow_ = -1;
    int     dlFile_ = 0;
    qint64  dlBytesBefore_ = 0;
    QString dlDir_;

    QString activeId_;
};
