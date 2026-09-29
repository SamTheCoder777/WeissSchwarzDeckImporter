#include "tcg_infer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <typeinfo>
#include <unordered_map>

#include <faiss/impl/IDSelector.h>

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

  return collapseToCandidates(idxs.data(), scores.data(), n_rows, top_k);
}

std::vector<Candidate> TcgInfer::searchFiltered(const cv::Mat &crop_bgr,
                                                int top_k,
                                                const std::vector<std::string> &allowed_codes)
{
    const std::unordered_set<std::string> allowed(allowed_codes.begin(), allowed_codes.end());
    const auto &card_ids = core_.card_ids();

    std::vector<int> slots;
    for (int i = 0; i < (int) card_ids.size(); ++i)
        if (allowed.count(card_ids[i]))
            slots.push_back(i);

    return searchBySlots(crop_bgr, top_k, slots);
}
std::vector<Candidate> TcgInfer::searchBySlots(const cv::Mat &crop_bgr,
                                               int top_k,
                                               const std::vector<int> &allowed_slots)
{
    if (top_k <= 0 || allowed_slots.empty())
        return {};

    const auto &card_to_rows = core_.card_to_rows();

    std::vector<float> q = core_.embed(crop_bgr);
    const faiss::Index *index = core_.index();
    if (!index)
        throw std::runtime_error("[TcgInfer] FAISS index is null!");

    const int64_t ntotal = index->ntotal;
    std::vector<uint8_t> bitmap((ntotal + 7) / 8, 0);
    int64_t allowed_rows = 0;

    for (int slot : allowed_slots) {
        if (slot < 0 || slot >= (int) card_to_rows.size())
            continue;
        for (int64_t r : card_to_rows[slot]) {
            bitmap[r >> 3] |= (uint8_t) (1u << (r & 7));
            ++allowed_rows;
        }
    }
    if (allowed_rows == 0)
        return {};

    faiss::IDSelectorBitmap sel(ntotal, bitmap.data());
    faiss::SearchParameters params;
    params.sel = &sel;

    int n_rows = (int) std::min<int64_t>(allowed_rows, (int64_t) top_k * core_.rows_per_card());
    std::vector<float> scores(n_rows);
    std::vector<int64_t> idxs(n_rows);
    index->search(1, q.data(), n_rows, scores.data(), idxs.data(), &params);

    return collapseToCandidates(idxs.data(), scores.data(), n_rows, top_k);
}

std::vector<Candidate> TcgInfer::collapseToCandidates(const int64_t *idxs,
                                                      const float *scores,
                                                      int n,
                                                      int top_k)
{
    const auto &row_to_card = core_.row_to_card();
    const auto &card_ids = core_.card_ids();

    std::unordered_map<int, float> best;
    for (int i = 0; i < n; ++i) {
        int64_t r = idxs[i];
        if (r < 0 || r >= (int64_t) row_to_card.size())
            continue;
        int c = row_to_card[(size_t) r];
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
    if ((int) ranked.size() > top_k)
        ranked.resize(top_k);

    std::vector<Candidate> out;
    out.reserve(ranked.size());
    for (auto &[c, s] : ranked)
        out.push_back(Candidate{card_ids[(size_t) c], s, ""});
    return out;
}
