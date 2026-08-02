// services/ModelService.h
#pragma once
#include <QObject>
#include <QFutureWatcher>
#include <memory>
#include "../retrieval/tcg_infer.h"
#include "../retrieval/CardDetector.h"

class ModelService : public QObject {
    Q_OBJECT
public:
    explicit ModelService(QObject* parent = nullptr);

    TCGRetriever* retriever() const { return retriever_.get(); }
    CardDetector* detector()  const { return detector_.get(); }
    bool isLoading() const { return loading_; }
    bool isLoaded() const {return loaded_;}
    bool isSilent() const {return silent_;}

    void load(const QString& onnx, const QString& indexDir, const QString& yolo,
              bool native, int imgSize, bool silent);

    void load(bool silent = false); // For when settings already set

    void setIndexDir(QString indexDir) {indexDir_ = indexDir;}
    void setSilent(bool silent) {silent_ = silent;}

    QString getIndexDir() const {return indexDir_;}
    QString indexDirForId(const QString& id);

signals:
    void loaded(bool ok, const QString& message);
    void loading(bool finished);
    void statusChanged(const QString& message);

private:
    std::unique_ptr<TCGRetriever> retriever_;
    std::unique_ptr<CardDetector> detector_;
    QFutureWatcher<TCGRetriever*> watcher_;
    QString onnx_;
    QString indexDir_;
    QString pendingYolo_;
    bool loading_ = false, silent_ = false;
    bool loaded_ = false;
};