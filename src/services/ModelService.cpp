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
    connect(&watcher_, &QFutureWatcher<TCGRetriever*>::finished, this, [this] {
        loading_ = false;

        TCGRetriever* r = watcher_.result();
        if (!r) {
            retriever_.reset();
            emit statusChanged("Model load FAILED.");
            if (!silent_)
                emit loaded(false, "Could not load model or index.");
            //pushStateToQml();
            return;
        }
        retriever_.reset(r);

        if (!pendingYolo_.isEmpty()) {
            try { detector_ = std::make_unique<CardDetector>(pendingYolo_.toStdString()); }
            catch (const std::exception& e) {
                detector_.reset();
                if (!silent_)
                    emit loaded(false, QString("YOLO load failed: %1").arg(e.what()));
            }
        }

        Config::instance().setCurModelPath(onnx_);
        Config::instance().setCurYoloModelPath(pendingYolo_);
        loaded_ = true;
        emit statusChanged("Model + index loaded OK.");
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

    QFuture<TCGRetriever*> fut = QtConcurrent::run(
        [onnx, indexDir, native_, imgSize_]() -> TCGRetriever* {
            try {
                return new TCGRetriever(onnx.toStdString(), indexDir.toStdString(), std::string(), native_, imgSize_);
            } catch (...) {
                return nullptr;
            }
        });
    watcher_.setFuture(fut);
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

QString ModelService::indexDirForId(const QString &id)
{
    if (id.isEmpty()) return {};
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString dir = base + "/indexes/" + id;
    return QDir(dir).exists() ? dir : QString();
}
