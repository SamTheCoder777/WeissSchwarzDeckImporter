// tcg_infer.cpp — see tcg_infer.h. Mirrors Python tcg_infer.py preprocessing exactly.
#include "tcg_infer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

#include <opencv2/imgproc.hpp>
#include <faiss/index_io.h>
#include <faiss/Index.h>
#include <thread>

// tiny JSON array-of-strings reader for id_map.json (avoids a JSON dep)
static std::vector<std::string> read_json_string_array(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '"') {
            std::string cur;
            for (++i; i < s.size() && s[i] != '"'; ++i) {
                if (s[i] == '\\' && i + 1 < s.size()) ++i;  // skip escape
                cur.push_back(s[i]);
            }
            out.push_back(cur);
        }
        ++i;
    }
    return out;
}

// read row2card.npy (int64 or int32, 1-D). Minimal .npy parser.
static std::vector<int> read_npy_int(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    char magic[6]; f.read(magic, 6);                        // \x93NUMPY
    unsigned char ver[2]; f.read((char*)ver, 2);
    uint16_t hlen; f.read((char*)&hlen, 2);
    std::string header(hlen, '\0'); f.read(&header[0], hlen);
    bool is64 = header.find("<i8") != std::string::npos || header.find("|i8") != std::string::npos;
    bool is32 = header.find("<i4") != std::string::npos;
    // element count = file remainder / element size
    std::streampos data_start = f.tellg();
    f.seekg(0, std::ios::end);
    std::streamoff bytes = f.tellg() - data_start;
    f.seekg(data_start);
    std::vector<int> out;
    if (is64) {
        size_t n = bytes / 8; out.resize(n);
        for (size_t i = 0; i < n; ++i) { int64_t v; f.read((char*)&v, 8); out[i] = (int)v; }
    } else if (is32) {
        size_t n = bytes / 4; out.resize(n);
        for (size_t i = 0; i < n; ++i) { int32_t v; f.read((char*)&v, 4); out[i] = (int)v; }
    } else {
        throw std::runtime_error("row2card.npy: unexpected dtype (need <i8 or <i4)");
    }
    return out;
}

TCGRetriever::TCGRetriever(const std::string& onnx_path, const std::string& index_dir,
                           const std::string& masters_dir, bool native, int img_size,
                           bool use_directml)
    : native_(native), S_(img_size), masters_dir_(masters_dir),
      env_(ORT_LOGGING_LEVEL_WARNING, "tcg") {
    so_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    so_.SetIntraOpNumThreads(std::max(1u, std::thread::hardware_concurrency() / 2));
    // NOTE: DirectML wiring intentionally omitted for the first build (CPU only).
    // To add later: #include <dml_provider_factory.h> and append the DML EP here.
    (void)use_directml;

#ifdef _WIN32
    std::wstring wpath(onnx_path.begin(), onnx_path.end());
    session_ = std::make_unique<Ort::Session>(env_, wpath.c_str(), so_);
#else
    session_ = std::make_unique<Ort::Session>(env_, onnx_path.c_str(), so_);
#endif

    Ort::AllocatorWithDefaultOptions alloc;
    size_t n_in = session_->GetInputCount();
    for (size_t i = 0; i < n_in; ++i) {
        auto name = session_->GetInputNameAllocated(i, alloc);
        input_names_.emplace_back(name.get());
    }
    for (auto& s : input_names_) input_name_ptrs_.push_back(s.c_str());
    auto out = session_->GetOutputNameAllocated(0, alloc);
    out_name_ = out.get();
    output_name_ptrs_.push_back(out_name_.c_str());

    // index files
    index_.reset(faiss::read_index((index_dir + "/index.faiss").c_str()));
    row2card_ = read_npy_int(index_dir + "/row2card.npy");
    card_ids_ = read_json_string_array(index_dir + "/id_map.json");
    out_dim_  = index_->d;
}

TCGRetriever::~TCGRetriever() = default;

// preprocess: letterbox (native) or square pad, then normalize to CHW float.
// MUST match Python preprocess_native / preprocess_square exactly.
std::vector<float> TCGRetriever::preprocess(const cv::Mat& crop_bgr, int64_t& gy, int64_t& gx) {
    const int S = S_, mult = patch_;
    const float MEAN[3] = {0.485f, 0.456f, 0.406f};   // RGB
    const float STD[3]  = {0.229f, 0.224f, 0.225f};
    const cv::Scalar FILL(114, 114, 114);

    cv::Mat rgb;
    cv::cvtColor(crop_bgr, rgb, cv::COLOR_BGR2RGB);
    int w = rgb.cols, h = rgb.rows;

    cv::Mat canvas(S, S, CV_8UC3, FILL);
    if (native_) {
        double scale = (double)S / std::max(w, h);
        auto roundmult = [&](int v) {
            int r = (int)std::llround(v * scale);
            r = std::max(mult, ((r + mult - 1) / mult) * mult);
            return std::min(S, r);
        };
        int nw = roundmult(w), nh = roundmult(h);
        cv::Mat resized;
        cv::resize(rgb, resized, cv::Size(nw, nh), 0, 0, cv::INTER_CUBIC);
        resized.copyTo(canvas(cv::Rect(0, 0, nw, nh)));     // TOP-LEFT
        gy = nh / mult; gx = nw / mult;
    } else {
        int m = std::max(w, h);
        cv::Mat sq(m, m, CV_8UC3, FILL);
        rgb.copyTo(sq(cv::Rect((m - w) / 2, (m - h) / 2, w, h)));  // centered
        cv::resize(sq, canvas, cv::Size(S, S), 0, 0, cv::INTER_CUBIC);
        gy = S / mult; gx = S / mult;
    }

    // HWC uint8 -> CHW float normalized
    std::vector<float> t((size_t)3 * S * S);
    for (int y = 0; y < S; ++y) {
        const cv::Vec3b* row = canvas.ptr<cv::Vec3b>(y);
        for (int x = 0; x < S; ++x) {
            for (int c = 0; c < 3; ++c) {
                float v = row[x][c] / 255.0f;
                t[(size_t)c * S * S + (size_t)y * S + x] = (v - MEAN[c]) / STD[c];
            }
        }
    }
    return t;
}

std::vector<float> TCGRetriever::embed(const cv::Mat& crop_bgr) {
    int64_t gy, gx;
    std::vector<float> img = preprocess(crop_bgr, gy, gx);

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<int64_t, 4> ishape{1, 3, S_, S_};
    std::vector<Ort::Value> inputs;
    inputs.push_back(Ort::Value::CreateTensor<float>(mem, img.data(), img.size(),
                                                     ishape.data(), ishape.size()));
    std::vector<int64_t> vg{gy, gx};
    std::array<int64_t, 2> vgshape{1, 2};
    if (input_names_.size() > 1) {   // native model has 'valid_grid'
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, vg.data(), vg.size(),
                                                           vgshape.data(), vgshape.size()));
    }

    auto out = session_->Run(Ort::RunOptions{nullptr},
                             input_name_ptrs_.data(), inputs.data(), inputs.size(),
                             output_name_ptrs_.data(), 1);
    float* p = out[0].GetTensorMutableData<float>();
    std::vector<float> v(p, p + out_dim_);

    // L2 normalize (cosine == inner product)
    float nrm = 0.f; for (float x : v) nrm += x * x;
    nrm = std::sqrt(nrm) + 1e-12f;
    for (float& x : v) x /= nrm;
    return v;
}

std::vector<Candidate> TCGRetriever::search(const cv::Mat& crop_bgr, int top_k) {
    std::vector<float> q = embed(crop_bgr);

    int n_rows = std::min<int>((int)index_->ntotal, std::max(top_k * rows_per_card_, 300));
    std::vector<float>   scores(n_rows);
    std::vector<int64_t> idxs(n_rows);
    index_->search(1, q.data(), n_rows, scores.data(), idxs.data());

    std::unordered_map<int, float> best;                 // card slot -> max score
    for (int i = 0; i < n_rows; ++i) {
        int64_t r = idxs[i];
        if (r < 0) continue;
        int c = row2card_[(size_t)r];
        auto it = best.find(c);
        if (it == best.end() || scores[i] > it->second) best[c] = scores[i];
    }

    std::vector<std::pair<int, float>> ranked(best.begin(), best.end());
    std::sort(ranked.begin(), ranked.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    if ((int)ranked.size() > top_k) ranked.resize(top_k);

    std::vector<Candidate> out;
    for (auto& [c, s] : ranked) {
        Candidate cand{card_ids_[(size_t)c], s, ""};
        if (!masters_dir_.empty()) {
            for (const char* ext : {".png", ".jpg", ".jpeg", ".webp"}) {
                std::string p = masters_dir_ + "/" + cand.card_id + ext;
                std::ifstream test(p);
                if (test.good()) { cand.master_path = p; break; }
            }
        }
        out.push_back(std::move(cand));
    }
    return out;
}
