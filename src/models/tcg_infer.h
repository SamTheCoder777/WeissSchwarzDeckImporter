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

private:
  TcgCore &core_;
};
