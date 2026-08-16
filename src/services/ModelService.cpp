#include "ModelService.h"
#include "../core/Config.h"

#include <QMessageBox>
#include <QtConcurrent>
#include <QStandardPaths>
#include <QDir>


ModelService::ModelService(QObject *parent):
    QObject(parent)
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
        emit statusChanged("Model + index loaded OK.");
        //pushStateToQml();
    });

    // Model index change watcher
    connect(&coreIndexChangeWatcher_, &QFutureWatcher<void>::finished, this, [this] {
        loading_ = false;
        emit statusChanged("Index successfully switched");
        //pushStateToQml();
    });
}

void ModelService::load(const QString &onnx, const QString &indexDir, const QString &yolo, bool native, int imgSize, bool silent)
{
    if (loading_) return;

    if (!silent && indexDir.isEmpty()) {
        //QMessageBox::warning(this, "Detector", "Index not set.\nDownload and click 'use'.");
        emit loaded(false, "Index not set.\nDownload and click 'use'.");
        return;
    }
    if (indexDir.isEmpty() || onnx.isEmpty()) return;

    onnx_ = onnx;
    indexDir_ = indexDir;
    const bool native_       = native;
    const int  imgSize_      = imgSize;
    pendingYolo_            = yolo;
    silent_            = silent;

    loading_ = true;
    loaded_ = false;
    //if (modelStatus_) modelStatus_->setText("Loading model, please wait…");
    emit statusChanged("Loading model, please wait…");

    //QApplication::setOverrideCursor(Qt::BusyCursor);

    QFuture<TcgCore *> fut = QtConcurrent::run([onnx, indexDir, native_, imgSize_]() -> TcgCore * {
        try {
            return new TcgCore(onnx.toStdString(),
                               indexDir.toStdString(),
                               std::string(),
                               native_,
                               imgSize_);
        } catch (...) {
            return nullptr;
        }
    });
    coreLoadWatcher_.setFuture(fut);
}

void ModelService::load(bool silent)
{
    Config& c = Config::instance();
    load(c.getCurModelPath(),
         indexDirForId(c.getCurIndexId()),
         c.getCurYoloModelPath(),
         c.getModelNative(),
         c.getModelImgSize(),
         silent);
}

void ModelService::changeIndex(const QString &indexDir)
{
    if (loading_ || !loaded_)
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

    QFuture<TcgCore *> fut = QtConcurrent::run([this]() -> TcgCore * {
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
    if (id.isEmpty()) return {};
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString dir = base + "/indexes/" + id;
    return QDir(dir).exists() ? dir : QString();
}

std::vector<Candidate> ModelService::search(const cv::Mat &cropBgr, int topK)
{
    std::lock_guard<std::mutex> lock(onnxMutex_);
    return tcgInfer_->search(cropBgr, topK);
}

bool ModelService::buildIndex(const QString &imageDir, const QString &saveDir, const int batchSize)
{
    std::unique_lock<std::mutex> lock(onnxMutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        emit loaded(false, "Busy searching — try building the index in a moment.");
        return false;
    }

    indexBuilder_->create_index_batched(imageDir.toStdString(), saveDir.toStdString(), batchSize);

    return true;
}
