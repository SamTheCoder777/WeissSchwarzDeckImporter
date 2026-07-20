// tcg_infer.h — headless retrieval core for the TCG desktop app.
// crop (cv::Mat BGR) -> preprocess (letterbox, MATCHING training) -> ONNXRuntime
//   -> 256-d query vector -> FAISS max-aggregation search -> top-K candidates.
//
// This mirrors the validated Python tcg_infer.py EXACTLY. The acceptance test:
//   w125-021_2.png must return bd_w125_021 with score ~0.775.
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <opencv2/core.hpp>
#include <onnxruntime_cxx_api.h>

namespace faiss { struct Index; }

struct Candidate {
    std::string card_id;
    float       score;
    std::string master_path;
};

class TCGRetriever {
public:
    // native=true  -> letterbox + valid_grid (matches native_aspect=True training)
    // native=false -> square pad (matches --mode square export)
    TCGRetriever(const std::string& onnx_path,
                 const std::string& index_dir,
                 const std::string& masters_dir = "",
                 bool  native   = true,
                 int   img_size = 336,
                 bool  use_directml = false);
    ~TCGRetriever();

    // Encode one crop into a normalized 256-d query vector.
    std::vector<float> embed(const cv::Mat& crop_bgr);

    // Search one crop -> ranked candidates (per-card max-aggregation).
    std::vector<Candidate> search(const cv::Mat& crop_bgr, int top_k = 15);


private:
    // preprocessing (must be byte-identical to the Python version)
    // returns CHW float tensor (1x3xSxS) and fills gy,gx (content token grid)
    std::vector<float> preprocess(const cv::Mat& crop_bgr, int64_t& gy, int64_t& gx);

    bool  native_;
    int   S_;
    int   patch_ = 16;
    std::string masters_dir_;

    // ORT
    Ort::Env            env_;
    Ort::SessionOptions so_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> input_names_;      // owns the strings
    std::vector<const char*> input_name_ptrs_;
    std::vector<const char*> output_name_ptrs_;
    std::string out_name_;

    // gallery / index
    std::unique_ptr<faiss::Index> index_;
    std::vector<int>         row2card_;
    std::vector<std::string> card_ids_;
    int out_dim_ = 256;
    int rows_per_card_ = 26;
};
