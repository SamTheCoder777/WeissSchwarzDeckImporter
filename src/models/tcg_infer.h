#pragma once

#include "tcg_core.h"

namespace faiss {
struct Index;
}

struct Candidate {
  std::string card_id;
  float score;
  std::string master_path;
};

class TcgInfer {
public:
  explicit TcgInfer(TcgCore &core) : core_(core) {}

  std::vector<Candidate> search(const cv::Mat &crop_bgr, int top_k = 15);
  std::vector<Candidate> searchFiltered(const cv::Mat &crop_bgr,
                                        int top_k,
                                        const std::vector<std::string> &allowed_codes);

  private:
  std::vector<Candidate> searchBySlots(const cv::Mat &crop_bgr,
                                       int top_k,
                                       const std::vector<int> &allowed_slots);

  std::vector<Candidate> collapseToCandidates(const int64_t *idxs,
                                              const float *scores,
                                              int n,
                                              int top_k);

  TcgCore &core_;
};
