#ifndef TRANSLATIONWORKER_H
#define TRANSLATIONWORKER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <onnxruntime_cxx_api.h>
#include <sentencepiece_processor.h>

class TranslationWorker : public QObject {
    Q_OBJECT
public:
    explicit TranslationWorker(const QString& encoderPath,
                               const QString& decoderPath,
                               const QString& spmSourcePath,
                               const QString& spmTargetPath,
                               const QString& vocabJsonPath,
                               const QString& addedTokensPath,
                               QObject* parent = nullptr);
    ~TranslationWorker();

    Q_INVOKABLE QString translate(const QString& japaneseText);

private:
    Ort::Env m_env;
    Ort::MemoryInfo m_memoryInfo;
    Ort::Session* m_encoderSession;
    Ort::Session* m_decoderSession;

    sentencepiece::SentencePieceProcessor m_spmSource;
    sentencepiece::SentencePieceProcessor m_spmTarget;

    QHash<QString, int64_t> m_vocab;
    QHash<int64_t, QString> m_inverseVocab;
    QVector<QString> m_extraTokens;

    void loadVocab(const QString& vocabJsonPath);
    void loadAddedTokens(const QString& addedTokensPath);
    QVector<int64_t> tokenize(const QString& text);
    QString detokenize(const QVector<int64_t>& tokens);
};

#endif // TRANSLATIONWORKER_H