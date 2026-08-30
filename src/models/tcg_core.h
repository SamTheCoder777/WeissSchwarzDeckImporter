#pragma once

#include <string>
#include <thread>
#include <vector>

#include <faiss/Index.h>
#include <faiss/impl/io.h>
#include <faiss/index_io.h>
#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

class TcgCore {
public:
    TcgCore(const std::string &onnx_path,
            bool native = true,
            int img_size = 336,
            bool use_directml = false);
    ~TcgCore() = default;

    int native() const { return native_; }
    int S() const { return S_; }
    int patch() const { return patch_; }

    std::vector<std::string> input_names() const { return input_names_; }
    std::vector<const char *> input_name_ptrs() const { return input_name_ptrs_; }
    std::vector<const char *> output_name_ptrs() const { return output_name_ptrs_; }

    const faiss::Index *index() const { return index_.get(); }
    std::vector<int> row_to_card() const { return row_to_card_; }
    std::vector<std::string> card_ids() const { return card_ids_; }
    int out_dim() const { return out_dim_; }
    int rows_per_card() const { return rows_per_card_; }

    // reload only the index without reloading the model
    void load_index(std::string index_dir);

    std::vector<float> preprocess(const cv::Mat &crop_bgr, int64_t &gy, int64_t &gx);

    // embed
    std::vector<float> embed(const cv::Mat &crop_bgr);
    std::vector<float> embed_batch(const std::vector<cv::Mat> &crops_bgr);

private:
  bool native_;
  int S_;
  int patch_ = 16;

  // ORT
  Ort::Env env_;
  Ort::SessionOptions so_;
  std::unique_ptr<Ort::Session> session_;
  std::vector<std::string> input_names_;
  std::vector<const char *> input_name_ptrs_;
  std::vector<const char *> output_name_ptrs_;
  std::string out_name_;

  // gallery / index
  std::unique_ptr<faiss::Index> index_;
  std::vector<int> row_to_card_;
  std::vector<std::string> card_ids_;
  int out_dim_ = 256;
  int rows_per_card_ = 26;
};