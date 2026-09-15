#include "tcg_infer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

std::vector<Candidate> TcgInfer::search(const cv::Mat &crop_bgr, int top_k) {
    std::vector<float> q = core_.embed(crop_bgr);

    const faiss::Index *index = core_.index();
    if (!index) {
        throw std::runtime_error("[TcgInfer] FAISS index is null!");
    }

  const int rows_per_card = core_.rows_per_card();

  const auto &row_to_card = core_.row_to_card();
  if (row_to_card.empty()) {
    throw std::runtime_error("[TcgInfer] row_to_card mapping is empty!");
  }

  const auto &card_ids = core_.card_ids();
  if (card_ids.empty()) {
    throw std::runtime_error("[TcgInfer] card_ids array is empty!");
  }

  int n_rows =
      std::min<int>((int)index->ntotal, std::max(top_k * rows_per_card, 300));
  std::vector<float> scores(n_rows);
  std::vector<int64_t> idxs(n_rows);
  index->search(1, q.data(), n_rows, scores.data(), idxs.data());

  std::vector<float> scores2(n_rows);
  std::vector<int64_t> idxs2(n_rows);
  index->search(1, q.data(), n_rows, scores2.data(), idxs2.data());

  std::unordered_map<int, float> best; // card slot -> max score
  for (int i = 0; i < n_rows; ++i) {
    int64_t r = idxs[i];
    if (r < 0)
      continue;
    if (r < 0 || r >= (int64_t) row_to_card.size())
        continue;
    int c = row_to_card[(size_t)r];
    if (c < 0 || c >= (int) card_ids.size())
        continue;
    auto it = best.find(c);
    if (it == best.end() || scores[i] > it->second)
      best[c] = scores[i];
  }

  std::vector<std::pair<int, float>> ranked(best.begin(), best.end());
  std::sort(ranked.begin(), ranked.end(), [](auto &a, auto &b) {
      if (a.second != b.second)
          return a.second > b.second;
      return a.first < b.first;
  });
  if ((int)ranked.size() > top_k)
    ranked.resize(top_k);

  std::vector<Candidate> out;
  for (auto &[c, s] : ranked) {
    Candidate cand{card_ids[(size_t)c], s, ""};
    // if (!masters_dir_.empty()) {
    //     for (const char* ext : {".png", ".jpg", ".jpeg", ".webp"}) {
    //         std::string p = masters_dir_ + "/" + cand.card_id + ext;
    //         std::ifstream test(p);
    //         if (test.good()) { cand.master_path = p; break; }
    //     }
    // }
    out.push_back(std::move(cand));
  }
  return out;
}
