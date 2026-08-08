#include "TranslationWorker.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <QDebug>

TranslationWorker::TranslationWorker(const QString& encoderPath,
                                     const QString& decoderPath,
                                     const QString& spmSourcePath,
                                     const QString& spmTargetPath,
                                     const QString& vocabJsonPath,
                                     const QString& addedTokensPath,
                                     QObject* parent)
    : QObject(parent),
    m_env(ORT_LOGGING_LEVEL_WARNING, "TranslationWorker"),
    m_memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
{
    Ort::SessionOptions sessionOptions;
    sessionOptions.SetIntraOpNumThreads(0);
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
    m_encoderSession = new Ort::Session(m_env, encoderPath.toStdWString().c_str(), sessionOptions);
    m_decoderSession = new Ort::Session(m_env, decoderPath.toStdWString().c_str(), sessionOptions);
#else
    m_encoderSession = new Ort::Session(m_env, encoderPath.toStdString().c_str(), sessionOptions);
    m_decoderSession = new Ort::Session(m_env, decoderPath.toStdString().c_str(), sessionOptions);
#endif

    m_spmSource.Load(spmSourcePath.toStdString());
    m_spmTarget.Load(spmTargetPath.toStdString());

    m_extraTokens = {
        "<TRAIT>", "<NAME>",
        "<SOUL>", "<CHOICE>", "<TREASURE>", "<SALVAGE>", "<STANDBY>",
        "<GATE>", "<BOUNCE>", "<STOCK>", "<SHOT>", "<DRAW>",
        QString::fromUtf8("【"), QString::fromUtf8("】"),
        "AUTO", "ACT", "CONT", "COUNTER", "CLOCK",
        QString::fromUtf8("トリガー")
    };

    loadVocab(vocabJsonPath);
    loadAddedTokens(addedTokensPath);
}

void TranslationWorker::loadAddedTokens(const QString& addedTokensPath) {
    QFile file(addedTokensPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Note: No added_tokens.json found at" << addedTokensPath << "- custom tokens might be skipped.";
        return;
    }

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject jsonObj = doc.object();

    for (auto it = jsonObj.begin(); it != jsonObj.end(); ++it) {
        QString tokenStr = it.key();
        int64_t tokenId = it.value().toVariant().toLongLong();

        // Merge into our existing maps
        m_vocab.insert(tokenStr, tokenId);
        m_inverseVocab.insert(tokenId, tokenStr);
    }
}

void TranslationWorker::loadVocab(const QString& vocabJsonPath) {
    QFile file(vocabJsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open vocab file:" << vocabJsonPath;
        return;
    }

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject jsonObj = doc.object();

    for (auto it = jsonObj.begin(); it != jsonObj.end(); ++it) {
        QString tokenStr = it.key();
        int64_t tokenId = it.value().toVariant().toLongLong();

        m_vocab.insert(tokenStr, tokenId);
        m_inverseVocab.insert(tokenId, tokenStr);
    }
}

TranslationWorker::~TranslationWorker() {
    delete m_encoderSession;
    delete m_decoderSession;
}
QVector<int64_t> TranslationWorker::tokenize(const QString& text) {
    QVector<int64_t> tokens;

    // 1. Build a regex to isolate all custom training tokens
    QString regexStr = "(";
    for (int i = 0; i < m_extraTokens.size(); ++i) {
        regexStr += QRegularExpression::escape(m_extraTokens[i]);
        if (i < m_extraTokens.size() - 1) regexStr += "|";
    }
    regexStr += ")";

    QRegularExpression re(regexStr);
    int offset = 0;
    QRegularExpressionMatchIterator i = re.globalMatch(text);
    int64_t unkId = m_vocab.value("<unk>", 3);

    // 2. Alternate between standard SentencePiece chunking and direct token mapping
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();

        QString beforeText = text.mid(offset, match.capturedStart() - offset);
        if (!beforeText.isEmpty()) {
            std::vector<std::string> pieces;
            m_spmSource.Encode(beforeText.toStdString(), &pieces);
            for (const std::string& piece : pieces) {
                QString qpiece = QString::fromStdString(piece);
                tokens.append(m_vocab.value(qpiece, unkId));
            }
        }

        QString extraToken = match.captured(1);
        QString spmPrefix = QString::fromUtf8("\xe2\x96\x81");
        QString spmExtraToken = spmPrefix + extraToken;

        if (m_vocab.contains(extraToken)) {
            tokens.append(m_vocab[extraToken]);
        } else if (m_vocab.contains(spmExtraToken)) {
            tokens.append(m_vocab[spmExtraToken]);
        } else {
            qWarning() << "Warning: Tag not found in vocab, falling back to UNK:" << extraToken;
            tokens.append(unkId);
        }

        offset = match.capturedEnd();
    }

    // 3. Process any remaining text
    QString remainingText = text.mid(offset);
    if (!remainingText.isEmpty()) {
        std::vector<std::string> pieces;
        m_spmSource.Encode(remainingText.toStdString(), &pieces);
        for (const std::string& piece : pieces) {
            QString qpiece = QString::fromStdString(piece);
            tokens.append(m_vocab.value(qpiece, unkId));
        }
    }

    return tokens;
}

QString TranslationWorker::translate(const QString& japaneseText) {
    QVector<int64_t> inputIds = tokenize(japaneseText);
    if (inputIds.isEmpty()) {
        return QString();
    }
    inputIds.append(0); // MarianTokenizer always appends eos_token_id (0) to the source sequence

    try {

        std::vector<int64_t> stdInputIds(inputIds.begin(), inputIds.end());
        std::vector<int64_t> attentionMask(stdInputIds.size(), 1);

        // 1. Run Encoder
        std::vector<int64_t> encoderInputShape = {1, static_cast<int64_t>(stdInputIds.size())};

        Ort::Value inputIdsTensor = Ort::Value::CreateTensor<int64_t>(
            m_memoryInfo, stdInputIds.data(), stdInputIds.size(), encoderInputShape.data(), encoderInputShape.size());

        Ort::Value attentionMaskTensor = Ort::Value::CreateTensor<int64_t>(
            m_memoryInfo, attentionMask.data(), attentionMask.size(), encoderInputShape.data(), encoderInputShape.size());

        const char* encoderInputNames[] = {"input_ids", "attention_mask"};
        const char* encoderOutputNames[] = {"last_hidden_state"};

        std::vector<Ort::Value> encoderInputs;
        encoderInputs.push_back(std::move(inputIdsTensor));
        encoderInputs.push_back(std::move(attentionMaskTensor));

        auto encoderOutputs = m_encoderSession->Run(
            Ort::RunOptions{nullptr},
            encoderInputNames, encoderInputs.data(), encoderInputs.size(),
            encoderOutputNames, 1
            );

        auto encTypeInfo = encoderOutputs[0].GetTensorTypeAndShapeInfo();
        auto encShape = encTypeInfo.GetShape();
        float* rawEncoderHiddenStates = encoderOutputs[0].GetTensorMutableData<float>();
        size_t encElementCount = encTypeInfo.GetElementCount();

        // 2. Decoder Setup
        // Per config.json: decoder_start_token_id == pad_token_id == 60715, eos_token_id == 0.
        // bad_words_ids == [[60715]], meaning the pad token must never be generated as
        // actual output -- it is only valid as the seed token fed into step 0.
        // config.json also specifies num_beams: 6, meaning this model was tuned/evaluated
        // with beam search. Plain greedy (argmax) decoding tends to drop rare/domain-specific
        // tokens (like custom bracket tags) and can lock into locally-plausible but globally
        // wrong word order, since it can never revisit an earlier choice. Beam search keeps
        // several candidate sequences alive at once and picks the best-scoring complete
        // sequence, which fixes both problems.
        //
        // IMPORTANT: m_decoderSession must be loaded from decoder_model_merged.onnx (not
        // decoder_model.onnx / decoder_with_past_model.onnx). The merged graph exposes a
        // single "use_cache_branch" bool input: false on step 0 (computes cross-attention
        // K/V fresh from encoder_hidden_states), true afterward (reuses cached K/V and only
        // extends the decoder self-attention cache by the one new token). This lets every
        // step after the first run in O(1) instead of O(sequence length), which matters a
        // lot once you're doing that 6x over for beam search.
        int64_t padTokenId = m_vocab.value("<pad>", 60715);
        int64_t eosTokenId = m_vocab.value("</s>", 0);

        const int numBeams = 3;      // reduce from 6 to 3
        const int maxSteps = 512;
        const float lengthPenalty = 1.0f; // 1.0 == plain average log-prob per token

        const int64_t numLayers = 6;   // config.json decoder_layers
        const int64_t numHeads = 8;    // config.json decoder_attention_heads
        const int64_t headDim = 64;    // config.json d_model(512) / decoder_attention_heads(8)
        const int64_t encSeqLen = static_cast<int64_t>(stdInputIds.size());

        // IMPORTANT: inspecting decoder_model_merged.onnx's internal If node shows that on
        // the cache branch (use_cache_branch=true), present.*.encoder.key/value is NOT a
        // real passthrough of the cross-attention cache -- it's a hardcoded empty Constant
        // (dims [0,8,1,64]). Cross-attention K/V is only ever computed correctly on the
        // else_branch (use_cache_branch=false, i.e. step 0). So the encoder cache must be
        // captured once at step 0 and reused unchanged forever after -- never re-captured
        // from a later step's output. Only the decoder self-attention cache (present.*.decoder,
        // which really is Concat(past, new) on both branches) grows per step.
        struct EncLayerCache {
            std::vector<float> key;
            std::vector<float> value;
        };
        using EncCachePtr = std::shared_ptr<std::array<EncLayerCache, 6>>;

        struct DecLayerCache {
            std::vector<float> key;
            std::vector<float> value;
        };
        using DecCachePtr = std::shared_ptr<std::array<DecLayerCache, 6>>;

        struct Beam {
            QVector<int64_t> tokens;   // full history, including leading pad seed token
            float score = 0.0f;        // cumulative log-probability
            int64_t lastToken = 0;     // most recently generated token (next step's input_ids)
            int64_t decoderPastLen = 0; // number of positions already in the decoder KV cache
            DecCachePtr decCache;       // this beam's own self-attention cache (shared until it diverges)
        };

        struct StepResult {
            std::vector<float> logits;
            DecCachePtr decCache;
            EncCachePtr encCache;   // only populated when captureEncoder is true (step 0)
            int64_t decoderPastLen = 0;
        };

        // Build the fixed input/output name lists once, in the exact order we will
        // populate the corresponding Ort::Value arrays each call.
        std::vector<std::string> inputNameStrings = {"encoder_attention_mask", "input_ids", "encoder_hidden_states"};
        for (int64_t L = 0; L < numLayers; ++L) {
            std::string prefix = "past_key_values." + std::to_string(L) + ".";
            inputNameStrings.push_back(prefix + "decoder.key");
            inputNameStrings.push_back(prefix + "decoder.value");
            inputNameStrings.push_back(prefix + "encoder.key");
            inputNameStrings.push_back(prefix + "encoder.value");
        }
        inputNameStrings.push_back("use_cache_branch");
        std::vector<const char*> decoderInputNames;
        for (const auto& s : inputNameStrings) decoderInputNames.push_back(s.c_str());

        std::vector<std::string> outputNameStrings = {"logits"};
        for (int64_t L = 0; L < numLayers; ++L) {
            std::string prefix = "present." + std::to_string(L) + ".";
            outputNameStrings.push_back(prefix + "decoder.key");
            outputNameStrings.push_back(prefix + "decoder.value");
            outputNameStrings.push_back(prefix + "encoder.key");
            outputNameStrings.push_back(prefix + "encoder.value");
        }
        std::vector<const char*> decoderOutputNames;
        for (const auto& s : outputNameStrings) decoderOutputNames.push_back(s.c_str());

        // Runs one decoder step for one beam. tokenId is the single newest token to feed.
        // prevDecCache/prevPastLen describe the decoder self-attention cache to extend
        // (nullptr means step 0: no real cache yet). encCacheIn is the FIXED cross-attention
        // cache captured once at step 0 (nullptr only on step 0 itself, before it exists).
        // captureEncoder should only be true on the step-0 call -- it tells this function to
        // save off the freshly-computed encoder cache for reuse on every later step, since
        // that's the only step where it's computed correctly (see note above).
        auto runDecoderStep = [&](int64_t tokenId, bool useCache, const DecCachePtr& prevDecCache, int64_t prevPastLen,
                                  const EncCachePtr& encCacheIn, bool captureEncoder) -> StepResult {
            std::vector<int64_t> curInputIds = {tokenId};
            std::vector<int64_t> decInputShape = {1, 1};
            std::vector<int64_t> decKVShape = {1, numHeads, prevPastLen, headDim};
            std::vector<int64_t> encKVShape = {1, numHeads, encSeqLen, headDim};
            std::vector<int64_t> boolShape = {1};
            bool useCacheFlag = useCache;

            std::vector<Ort::Value> inputs;
            inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                m_memoryInfo, attentionMask.data(), attentionMask.size(), encoderInputShape.data(), encoderInputShape.size()));
            inputs.push_back(Ort::Value::CreateTensor<int64_t>(
                m_memoryInfo, curInputIds.data(), curInputIds.size(), decInputShape.data(), decInputShape.size()));
            inputs.push_back(Ort::Value::CreateTensor<float>(
                m_memoryInfo, rawEncoderHiddenStates, encElementCount, encShape.data(), encShape.size()));

            // Dummy buffers used only when there's no real cache yet (step 0). Declared
            // outside the loop so they stay alive until Run() is called below. The decoder
            // one is sized to 1 element (not 0) purely so .data() is guaranteed non-null --
            // the actual tensor element count passed to CreateTensor below is still 0,
            // matching the zero-length shape dimension.
            std::vector<float> dummyDecKV(1, 0.0f);
            std::vector<float> dummyEncKV(numHeads * encSeqLen * headDim, 0.0f);

            for (int64_t L = 0; L < numLayers; ++L) {
                if (prevDecCache) {
                    auto& lc = (*prevDecCache)[L];
                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        m_memoryInfo, lc.key.data(), lc.key.size(), decKVShape.data(), decKVShape.size()));
                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        m_memoryInfo, lc.value.data(), lc.value.size(), decKVShape.data(), decKVShape.size()));
                } else {
                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        m_memoryInfo, dummyDecKV.data(), 0, decKVShape.data(), decKVShape.size()));
                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        m_memoryInfo, dummyDecKV.data(), 0, decKVShape.data(), decKVShape.size()));
                }
                if (encCacheIn) {
                    auto& lc = (*encCacheIn)[L];
                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        m_memoryInfo, lc.key.data(), lc.key.size(), encKVShape.data(), encKVShape.size()));
                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        m_memoryInfo, lc.value.data(), lc.value.size(), encKVShape.data(), encKVShape.size()));
                } else {
                    // FIX: Use an empty shape {1, numHeads, 0, headDim} instead of encKVShape
                    std::vector<int64_t> emptyEncKVShape = {1, numHeads, 0, headDim};

                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        m_memoryInfo, dummyEncKV.data(), 0, emptyEncKVShape.data(), emptyEncKVShape.size()));
                    inputs.push_back(Ort::Value::CreateTensor<float>(
                        m_memoryInfo, dummyEncKV.data(), 0, emptyEncKVShape.data(), emptyEncKVShape.size()));
                }
            }
            inputs.push_back(Ort::Value::CreateTensor<bool>(
                m_memoryInfo, &useCacheFlag, 1, boolShape.data(), boolShape.size()));

            std::vector<Ort::Value> outputs;
            try {
                outputs = m_decoderSession->Run(
                    Ort::RunOptions{nullptr},
                    decoderInputNames.data(), inputs.data(), inputs.size(),
                    decoderOutputNames.data(), decoderOutputNames.size()
                    );
            } catch (const Ort::Exception& e) {
                qWarning() << "Decoder Run() failed:" << e.what()
                << "| useCache=" << useCache
                << "| prevPastLen=" << prevPastLen
                << "| inputs=" << inputs.size()
                << "| expected input names=" << decoderInputNames.size();
                throw; // rethrow so translate() can decide how to fail; see catch there
            }

            int64_t vocabSize = outputs[0].GetTensorTypeAndShapeInfo().GetShape().back();
            float* logitsPtr = outputs[0].GetTensorMutableData<float>();

            StepResult result;
            result.logits.assign(logitsPtr, logitsPtr + vocabSize);
            result.decoderPastLen = prevPastLen + 1;
            result.decCache = std::make_shared<std::array<DecLayerCache, 6>>();
            if (captureEncoder) {
                result.encCache = std::make_shared<std::array<EncLayerCache, 6>>();
            }

            for (int64_t L = 0; L < numLayers; ++L) {
                int outBase = 1 + static_cast<int>(L) * 4;

                auto dkInfo = outputs[outBase + 0].GetTensorTypeAndShapeInfo();
                size_t dkCount = dkInfo.GetElementCount();

                float* pdk = outputs[outBase + 0].GetTensorMutableData<float>();
                float* pdv = outputs[outBase + 1].GetTensorMutableData<float>();

                (*result.decCache)[L].key.assign(pdk, pdk + dkCount);
                (*result.decCache)[L].value.assign(pdv, pdv + dkCount);

                // present.*.encoder.key/value is only real on the else_branch (step 0).
                // On the cache branch it's a dummy empty Constant -- do not read it there.
                if (captureEncoder) {
                    auto ekInfo = outputs[outBase + 2].GetTensorTypeAndShapeInfo();
                    size_t ekCount = ekInfo.GetElementCount();
                    float* pek = outputs[outBase + 2].GetTensorMutableData<float>();
                    float* pev = outputs[outBase + 3].GetTensorMutableData<float>();
                    (*result.encCache)[L].key.assign(pek, pek + ekCount);
                    (*result.encCache)[L].value.assign(pev, pev + ekCount);
                }
            }

            return result;
        };

        // Applies the repetition penalty + bad-word suppression + log-softmax that used
        // to live inline in the loop, shared by both step 0 and the main loop below.
        auto scoreLogits = [&](std::vector<float>& logits, const QVector<int64_t>& history) {
            int64_t vocabSize = static_cast<int64_t>(logits.size());
            float repetitionPenalty = 1.0f;
            for (int64_t prevToken : history) {
                if (prevToken >= 0 && prevToken < vocabSize) {
                    if (logits[prevToken] < 0) logits[prevToken] *= repetitionPenalty;
                    else logits[prevToken] /= repetitionPenalty;
                }
            }
            if (padTokenId >= 0 && padTokenId < vocabSize) {
                logits[padTokenId] = -std::numeric_limits<float>::infinity();
            }
            float maxLogit = *std::max_element(logits.begin(), logits.end());
            double sumExp = 0.0;
            for (float v : logits) sumExp += std::exp(static_cast<double>(v - maxLogit));
            return maxLogit + static_cast<float>(std::log(sumExp)); // logSumExp
        };

        QVector<Beam> beams;
        QVector<Beam> finishedBeams;
        EncCachePtr encCache; // captured once at step 0, reused unchanged for every later step

        // --- Step 0: seed with the pad token, use_cache_branch = false ---
        {
            StepResult r0 = runDecoderStep(padTokenId, false, nullptr, 0, nullptr, /*captureEncoder=*/true);
            encCache = r0.encCache;
            QVector<int64_t> seedHistory = {padTokenId};
            float logSumExp = scoreLogits(r0.logits, seedHistory);
            int64_t vocabSize = static_cast<int64_t>(r0.logits.size());

            std::vector<int64_t> indices(vocabSize);
            for (int64_t v = 0; v < vocabSize; ++v) indices[v] = v;
            int k = static_cast<int>(std::min<int64_t>(numBeams, vocabSize));
            std::partial_sort(indices.begin(), indices.begin() + k, indices.end(),
                              [&](int64_t a, int64_t c) { return r0.logits[a] > r0.logits[c]; });

            for (int j = 0; j < k; ++j) {
                int64_t tokenId = indices[j];
                float logProb = r0.logits[tokenId] - logSumExp;

                Beam beam;
                beam.tokens = {padTokenId, tokenId};
                beam.score = logProb;
                beam.lastToken = tokenId;
                beam.decoderPastLen = r0.decoderPastLen;
                beam.decCache = r0.decCache; // shared starting point; steps below only ever read it

                if (tokenId == eosTokenId) finishedBeams.append(beam);
                else beams.append(beam);
            }
        }

        // --- Steps 1..maxSteps-1: use_cache_branch = true ---
        struct Candidate {
            int beamIdx;
            int64_t tokenId;
            float score;
            DecCachePtr decCache;
            int64_t decoderPastLen;
        };

        for (int step = 1; step < maxSteps && !beams.isEmpty(); ++step) {
            QVector<Candidate> candidates;

            for (int b = 0; b < beams.size(); ++b) {
                const Beam& beam = beams[b];
                StepResult r = runDecoderStep(beam.lastToken, true, beam.decCache, beam.decoderPastLen, encCache, /*captureEncoder=*/false);
                float logSumExp = scoreLogits(r.logits, beam.tokens);
                int64_t vocabSize = static_cast<int64_t>(r.logits.size());

                std::vector<int64_t> indices(vocabSize);
                for (int64_t v = 0; v < vocabSize; ++v) indices[v] = v;
                int k = static_cast<int>(std::min<int64_t>(numBeams, vocabSize));
                std::partial_sort(indices.begin(), indices.begin() + k, indices.end(),
                                  [&](int64_t a, int64_t c) { return r.logits[a] > r.logits[c]; });

                for (int j = 0; j < k; ++j) {
                    int64_t tokenId = indices[j];
                    float logProb = r.logits[tokenId] - logSumExp;
                    candidates.append({b, tokenId, beam.score + logProb, r.decCache, r.decoderPastLen});
                }
            }

            std::sort(candidates.begin(), candidates.end(),
                      [](const Candidate& x, const Candidate& y) { return x.score > y.score; });

            QVector<Beam> nextBeams;
            for (int c = 0; c < candidates.size() && nextBeams.size() < numBeams; ++c) {
                const Candidate& cand = candidates[c];
                Beam newBeam;
                newBeam.tokens = beams[cand.beamIdx].tokens;
                newBeam.tokens.append(cand.tokenId);
                newBeam.score = cand.score;
                newBeam.lastToken = cand.tokenId;
                newBeam.decoderPastLen = cand.decoderPastLen;
                newBeam.decCache = cand.decCache; // shared_ptr copy, cheap -- cache itself isn't duplicated

                if (cand.tokenId == eosTokenId) finishedBeams.append(newBeam);
                else nextBeams.append(newBeam);
            }

            beams = nextBeams;
            if (finishedBeams.size() >= numBeams) break;
        }

        // Prefer completed (EOS-terminated) beams; fall back to whatever is still
        // active if nothing reached EOS within maxSteps
        QVector<Beam>& pool = !finishedBeams.isEmpty() ? finishedBeams : beams;
        if (pool.isEmpty()) {
            return QString();
        }

        const Beam* best = nullptr;
        float bestNormScore = -std::numeric_limits<float>::infinity();
        for (const Beam& beam : pool) {
            // Length-normalize so beam search doesn't unfairly favor shorter sequences
            float norm = beam.score / std::pow(static_cast<float>(beam.tokens.size()), lengthPenalty);
            if (norm > bestNormScore) {
                bestNormScore = norm;
                best = &beam;
            }
        }

        // Strip the leading pad seed token (and any trailing eos/pad) before detokenizing
        QVector<int64_t> tokensToDecode;
        for (int i = 1; i < best->tokens.size(); ++i) {
            if (best->tokens[i] != padTokenId && best->tokens[i] != eosTokenId) {
                tokensToDecode.append(best->tokens[i]);
            }
        }

        return detokenize(tokensToDecode);

    } catch (const Ort::Exception& e) {
        qWarning() << "TranslationWorker::translate ONNX Runtime error:" << e.what();
        return QString();
    } catch (const std::exception& e) {
        qWarning() << "TranslationWorker::translate error:" << e.what();
        return QString();
    }
}

QString TranslationWorker::detokenize(const QVector<int64_t>& tokens) {
    QString result;
    std::vector<std::string> piecesBuffer;

    int64_t padTokenId = m_vocab.value("<pad>", 60715);

    QString spmPrefix = QString::fromUtf8("\xe2\x96\x81");

    for (int64_t token : tokens) {
        if (token == 0 || token == padTokenId) continue;

        QString str = m_inverseVocab.value(token, "");
        QString cleanStr = str;

        // Strip the SentencePiece prefix before checking if it's a special tag
        if (cleanStr.startsWith(spmPrefix)) {
            cleanStr = cleanStr.mid(spmPrefix.length());
        }

        if (m_extraTokens.contains(cleanStr)) {
            if (!piecesBuffer.empty()) {
                std::string detok;
                m_spmTarget.Decode(piecesBuffer, &detok);
                result += QString::fromStdString(detok);
                piecesBuffer.clear();
            }
            result += cleanStr; // Append the clean tag directly
        } else {
            piecesBuffer.push_back(str.toStdString()); // Send the original prefixed string to SPM
        }
    }

    if (!piecesBuffer.empty()) {
        std::string detok;
        m_spmTarget.Decode(piecesBuffer, &detok);
        result += QString::fromStdString(detok);
    }

    return result;
}