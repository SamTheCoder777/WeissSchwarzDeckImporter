#include "ModelService.h"
#include "../core/Config.h"

#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QtConcurrent>

static QString indexDirForId(const QString &id)
{
    return Config::instance().getIndexInstallPath() + "/" + id;
}

ModelService::ModelService(QObject *parent)
    : QObject(parent)
{
    // Model load watcher
    connect(&coreLoadWatcher_, &QFutureWatcher<TcgCore *>::finished, this, [this] {
        loading_ = false;

        TcgCore *r = coreLoadWatcher_.result();
        if (!r) {
            tcgCore_.reset();
            emit statusChanged("Model load FAILED.");
            if (!silent_)
                emit loaded(false, "Could not load model or index.");
            //pushStateToQml();
            return;
        }
        tcgCore_.reset(r);

        if (!pendingYolo_.isEmpty()) {
            try {
                detector_ = std::make_unique<CardDetector>(pendingYolo_.toStdString());
            } catch (const std::exception &e) {
                detector_.reset();
                if (!silent_)
                    emit loaded(false, QString("YOLO load failed: %1").arg(e.what()));
            }
        }

        // Set up infer and builder
        tcgInfer_ = std::make_unique<TcgInfer>(*tcgCore_);
        indexBuilder_ = std::make_unique<IndexBuilder>(*tcgCore_);

        Config::instance().setCurModelPath(onnx_);
        Config::instance().setCurYoloModelPath(pendingYolo_);
        loaded_ = true;
        emit loaded(true, "");
        emit statusChanged("Model loaded OK.");

        // attempt to load prev index
        const QString id = Config::instance().getCurIndexId();
        const QString dir = indexDirForId(id);

        if (id.isEmpty() || !QFile::exists(dir + "/index.faiss")) {
            emit statusChanged("Model loaded without index");
            if (!silent_)
                emit loaded(false, "The selected index is missing. Choose or download an index.");
            return;
        }

        try {
            tcgCore_->load_index(dir.toStdString());
        } catch (const std::exception &e) {
            emit statusChanged("Model loaded without index");
            if (!silent_)
                emit loaded(false, QString("Index load failed: %1").arg(e.what()));
            return;
        }

        emit statusChanged("Model + index loaded OK.");
    });

    // Model index change watcher
    connect(&coreIndexChangeWatcher_, &QFutureWatcher<void>::finished, this, [this] {
        loading_ = false;
        emit statusChanged("Index successfully switched");
        //pushStateToQml();
    });

    // Model index build watcher
    connect(&builderWatcher_, &QFutureWatcher<bool>::finished, this, [this] {
        const bool ok = builderWatcher_.result();
        setBusy(false);
        emit indexBuildFinished(ok);
    });
}

void ModelService::load(
    const QString &onnx, const QString &yolo, bool native, int imgSize, bool silent)
{
    if (loading_ || busy_)
        return;

    if (yolo.isEmpty() || onnx.isEmpty())
        return;

    onnx_ = onnx;
    const bool native_ = native;
    const int imgSize_ = imgSize;
    pendingYolo_ = yolo;
    silent_ = silent;

    loading_ = true;
    loaded_ = false;
    //if (modelStatus_) modelStatus_->setText("Loading model, please wait…");
    emit statusChanged("Loading model, please wait…");

    //QApplication::setOverrideCursor(Qt::BusyCursor);

    QFuture<TcgCore *> fut = QtConcurrent::run([onnx, native_, imgSize_]() -> TcgCore * {
        try {
            return new TcgCore(onnx.toStdString(), native_, imgSize_, true);
        } catch (...) {
            return nullptr;
        }
    });
    coreLoadWatcher_.setFuture(fut);
}

void ModelService::load(bool silent)
{
    Config &c = Config::instance();
    load(c.getCurModelPath(),
         c.getCurYoloModelPath(),
         c.getModelNative(),
         c.getModelImgSize(),
         silent);
}

void ModelService::changeIndex(const QString &indexDir)
{
    if (loading_ || !loaded_ || busy_)
        return;

    if (indexDir.isEmpty()) {
        emit loaded(false, "Index dir empty");
        return;
    }

    if (!tcgCore_) {
        emit loaded(false, "Model not loaded yet");
        return;
    }

    loading_ = true;

    indexDir_ = indexDir;

    emit statusChanged("Switching index, please wait…");

    QFuture<void> fut = QtConcurrent::run([this]() {
        try {
            tcgCore_->load_index(indexDir_.toStdString());
        } catch (const std::exception &e) {
            statusChanged("Error switching index: " + QString(e.what()));
        }
    });
    coreIndexChangeWatcher_.setFuture(fut);
}

QString ModelService::indexDirForId(const QString &id)
{
    if (id.isEmpty())
        return {};
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString dir = base + "/indexes/" + id;
    return QDir(dir).exists() ? dir : QString();
}

std::vector<Candidate> ModelService::search(const cv::Mat &cropBgr, int topK)
{
    std::lock_guard<std::mutex> lock(onnxMutex_);
    return tcgInfer_->search(cropBgr, topK);
}

void ModelService::buildIndex(const QString &imageDir, const QString &saveDir, int batchSize)
{
    if (busy_) {
        emit indexLog("Busy — can't build index right now.");
        return;
    }
    setBusy(true);
    indexBuilder_->resetCancel();
    emit indexLog("Starting index build…");

    QFuture<bool> fut = QtConcurrent::run([this, imageDir, saveDir, batchSize]() -> bool {
        std::unique_lock<std::mutex> lock(onnxMutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            emit indexLog("Busy searching — try building the index in a moment.");
            return false;
        }
        try {
            indexBuilder_->create_index_batched(imageDir.toStdString(),
                                                saveDir.toStdString(),
                                                batchSize,
                                                Config::instance().getDisableNameCheck(),
                                                [this](const QString &msg, int done, int total) {
                                                    emit indexLog(msg);
                                                    emit indexProgress(done, total);
                                                });
        } catch (const std::exception &e) {
            emit indexLog(QString("Error: %1").arg(e.what()));
            return false;
        }
        return true;
    });
    builderWatcher_.setFuture(fut);
}

void ModelService::cancelIndexBuild()
{
    if (indexBuilder_)
        indexBuilder_->requestCancel();
    emit indexLog("Cancelling after current batch…");
}
