// services/ModelService.h
#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <memory>
#include <mutex>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "../models/index_builder.h"
#include "../models/tcg_core.h"
#include "../models/tcg_infer.h"

#include "../retrieval/CardDetector.h"

class ModelService : public QObject {
    Q_OBJECT
public:
    explicit ModelService(QObject* parent = nullptr);

    TcgCore *tcgCore() const { return tcgCore_.get(); }
    CardDetector* detector()  const { return detector_.get(); }
    bool isLoading() const { return loading_; }
    bool isLoaded() const {return loaded_;}
    bool isSilent() const {return silent_;}
    void setBusy(bool b)
    {
        if (busy_ != b) {
            busy_ = b;
            emit busyChanged();
        }
    }
    bool busy() const { return busy_; }

    void load(const QString& onnx, const QString& indexDir, const QString& yolo,
              bool native, int imgSize, bool silent);

    void load(bool silent = false); // For when settings already set

    void changeIndex(const QString &indexDir); // Only change the index

    void setIndexDir(QString indexDir) {indexDir_ = indexDir;}
    void setSilent(bool silent) {silent_ = silent;}

    QString getIndexDir() const {return indexDir_;}
    QString indexDirForId(const QString& id);

    // faiss index search
    std::vector<Candidate> search(const cv::Mat &cropBgr, int topK);
    // faiss build index
    bool buildIndex(const QString &imageDir, const QString &saveDir, const int batchSize);
signals:
    void loaded(bool ok, const QString &message);
    void loading(bool finished);
    void statusChanged(const QString& message);
    void busyChanged();

private:
    // Faiss card model
    std::unique_ptr<TcgCore> tcgCore_;
    std::unique_ptr<IndexBuilder> indexBuilder_;
    std::unique_ptr<TcgInfer> tcgInfer_;

    std::unique_ptr<CardDetector> detector_;

    QFutureWatcher<TcgCore *> coreLoadWatcher_;
    QFutureWatcher<void> coreIndexChangeWatcher_;
    QFutureWatcher<IndexBuilder *> builderWatcher_;
    QFutureWatcher<TcgInfer *> inferWatcher_;

    std::mutex onnxMutex_;

    QString onnx_;
    QString indexDir_;
    QString pendingYolo_;
    bool loading_ = false, silent_ = false;
    bool loaded_ = false;
    std::atomic<bool> busy_{false};
};