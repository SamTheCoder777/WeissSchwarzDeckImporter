#include "tcg_core.h"

#define NOMINMAX
#include <QDebug>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

#if defined(_WIN32)
#include <dml_provider_factory.h>
#elif defined(__APPLE__)
#include <coreml_provider_factory.h>
#endif

#include "tcg_util.h"

TcgCore::TcgCore(const std::string &onnx_path, bool native, int img_size, bool acceleration)
    : native_(native)
    , S_(img_size)
    , env_(ORT_LOGGING_LEVEL_WARNING, "tcg")
{
    so_.SetGraphOptimizationLevel(acceleration ? GraphOptimizationLevel::ORT_ENABLE_BASIC
                                               : GraphOptimizationLevel::ORT_ENABLE_ALL);
    so_.SetIntraOpNumThreads(std::max(1u, std::thread::hardware_concurrency() / 2));
    so_.SetInterOpNumThreads(1);
    so_.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

    cv::setNumThreads(std::max(1, static_cast<int>(std::thread::hardware_concurrency() / 2)));

    if (acceleration) {
#if defined(_WIN32)
        so_.DisableMemPattern();
        OrtSessionOptionsAppendExecutionProvider_DML(so_, 0);
#elif defined(__APPLE__)
    // Disable due to bad performance
    // OrtSessionOptionsAppendExecutionProvider_CoreML(so_, 0);
#endif
  }

#ifdef _WIN32
  std::wstring wpath =
      TcgUtil::utf8_to_wide(onnx_path); // proper UTF-8 -> UTF-16
  const auto *path_ptr = wpath.c_str();
#else
  const auto *path_ptr = onnx_path.c_str();
#endif

  try {
    session_ = std::make_unique<Ort::Session>(env_, path_ptr, so_);
  } catch (const Ort::Exception &e) {
    if (acceleration) {
      std::cerr << "[WARN] DirectML/CoreML session failed (" << e.what()
                << "). Falling back to CPU.\n";

      Ort::SessionOptions cpu_so;
      cpu_so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
      cpu_so.SetIntraOpNumThreads(
          std::max(1u, std::thread::hardware_concurrency() / 2));

      session_ = std::make_unique<Ort::Session>(env_, path_ptr, cpu_so);
    } else {
      throw;
    }
  }

  Ort::AllocatorWithDefaultOptions alloc;
  size_t n_in = session_->GetInputCount();
  for (size_t i = 0; i < n_in; ++i) {
    auto name = session_->GetInputNameAllocated(i, alloc);
    input_names_.emplace_back(name.get());
  }
  for (auto &s : input_names_)
    input_name_ptrs_.push_back(s.c_str());
  auto out = session_->GetOutputNameAllocated(0, alloc);
  out_name_ = out.get();
  output_name_ptrs_.push_back(out_name_.c_str());
}

std::vector<float> TcgCore::preprocess(const cv::Mat &crop_bgr, int64_t &gy,
                                       int64_t &gx) {
  const int S = S_, mult = patch_;
  const float MEAN[3] = {0.485f, 0.456f, 0.406f}; // RGB
  const float STD[3] = {0.229f, 0.224f, 0.225f};
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
    resized.copyTo(canvas(cv::Rect(0, 0, nw, nh))); // TOP-LEFT
    gy = nh / mult;
    gx = nw / mult;
  } else {
    int m = std::max(w, h);
    cv::Mat sq(m, m, CV_8UC3, FILL);
    rgb.copyTo(sq(cv::Rect((m - w) / 2, (m - h) / 2, w, h))); // centered
    cv::resize(sq, canvas, cv::Size(S, S), 0, 0, cv::INTER_CUBIC);
    gy = S / mult;
    gx = S / mult;
  }

  // HWC uint8 -> CHW float normalized
  std::vector<float> t((size_t)3 * S * S);
  for (int y = 0; y < S; ++y) {
    const cv::Vec3b *row = canvas.ptr<cv::Vec3b>(y);
    for (int x = 0; x < S; ++x) {
      for (int c = 0; c < 3; ++c) {
        float v = row[x][c] / 255.0f;
        t[(size_t)c * S * S + (size_t)y * S + x] = (v - MEAN[c]) / STD[c];
      }
    }
  }
  return t;
}

std::vector<float> TcgCore::embed(const cv::Mat &crop_bgr) {
  int64_t gy, gx;
  std::vector<float> img = preprocess(crop_bgr, gy, gx);

  Ort::MemoryInfo mem =
      Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  std::array<int64_t, 4> ishape{1, 3, S_, S_};
  std::vector<Ort::Value> inputs;
  inputs.push_back(Ort::Value::CreateTensor<float>(
      mem, img.data(), img.size(), ishape.data(), ishape.size()));
  std::vector<int64_t> vg{gy, gx};
  std::array<int64_t, 2> vgshape{1, 2};
  if (input_names_.size() > 1) { // native model has 'valid_grid'
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
        mem, vg.data(), vg.size(), vgshape.data(), vgshape.size()));
  }

  auto out =
      session_->Run(Ort::RunOptions{nullptr}, input_name_ptrs_.data(),
                    inputs.data(), inputs.size(), output_name_ptrs_.data(), 1);
  float *p = out[0].GetTensorMutableData<float>();
  std::vector<float> v(p, p + out_dim_);

  // L2 normalize
  float nrm = 0.f;
  for (float x : v)
    nrm += x * x;
  nrm = std::sqrt(nrm) + 1e-12f;
  for (float &x : v)
    x /= nrm;
  return v;
}

std::vector<float> TcgCore::embed_batch(const std::vector<cv::Mat> &crops_bgr) {
  int64_t B = crops_bgr.size();
  if (B == 0)
    return {};

  std::vector<float> batch_imgs(B * 3 * S_ * S_);
  std::vector<int64_t> batch_vgs(B * 2);

  for (int64_t i = 0; i < B; ++i) {
    int64_t gy, gx;
    std::vector<float> img_data = preprocess(crops_bgr[i], gy, gx);

    std::copy(img_data.begin(), img_data.end(),
              batch_imgs.begin() + i * 3 * S_ * S_);
    batch_vgs[i * 2 + 0] = gy;
    batch_vgs[i * 2 + 1] = gx;
  }

  Ort::MemoryInfo mem =
      Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

  std::array<int64_t, 4> ishape{B, 3, S_, S_};
  std::vector<Ort::Value> inputs;
  inputs.push_back(Ort::Value::CreateTensor<float>(
      mem, batch_imgs.data(), batch_imgs.size(), ishape.data(), ishape.size()));

  std::array<int64_t, 2> vgshape{B, 2};
  if (input_names_.size() > 1) {
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(
        mem, batch_vgs.data(), batch_vgs.size(), vgshape.data(),
        vgshape.size()));
  }

  auto out =
      session_->Run(Ort::RunOptions{nullptr}, input_name_ptrs_.data(),
                    inputs.data(), inputs.size(), output_name_ptrs_.data(), 1);

  float *p = out[0].GetTensorMutableData<float>();
  std::vector<float> v(p, p + B * out_dim_);

  // L2 normalize
  for (int64_t i = 0; i < B; ++i) {
    float *emb = &v[i * out_dim_];
    float nrm = 0.f;
    for (int j = 0; j < out_dim_; ++j)
      nrm += emb[j] * emb[j];
    nrm = std::sqrt(nrm) + 1e-12f;
    for (int j = 0; j < out_dim_; ++j)
      emb[j] /= nrm;
  }
  return v;
}

void TcgCore::load_index(std::string index_dir) {
  if (index_dir.empty())
    return;
#ifdef _WIN32
  std::ifstream faiss_file(TcgUtil::utf8_to_wide(index_dir + "/index.faiss"),
                           std::ios::binary);
#else
  std::ifstream faiss_file(index_dir + "/index.faiss", std::ios::binary);
#endif
  if (!faiss_file) {
    // no faiss yet
    index_.reset();
    row_to_card_.clear();
    card_ids_.clear();
    return;
  }
  std::vector<uint8_t> faiss_buf((std::istreambuf_iterator<char>(faiss_file)),
                                 std::istreambuf_iterator<char>());
  faiss::VectorIOReader faiss_reader;
  faiss_reader.data = std::move(faiss_buf);
  index_.reset(faiss::read_index(&faiss_reader));
  row_to_card_ = TcgUtil::read_npy_int(index_dir + "/row2card.npy");
  card_ids_ = TcgUtil::read_json_string_array(index_dir + "/id_map.json");
  out_dim_ = index_->d;
}