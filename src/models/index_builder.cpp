#include "index_builder.h"

#include <iostream>
#include <stdexcept>
#include <unordered_map>

#include <faiss/IndexFlat.h>
#include <faiss/index_io.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "tcg_util.h"

namespace fs = std::filesystem;

void IndexBuilder::create_index_batched(const std::string &image_dir,
                                        const std::string &save_dir,
                                        int batch_size,
                                        ProgressFn progress)
{
    std::vector<float> all_embeddings;
    std::vector<int> new_row2card;
    std::vector<std::string> new_card_ids;
    std::unordered_map<std::string, int> card_id_to_slot;

    std::vector<cv::Mat> batch_images;
    std::vector<std::string> batch_card_ids;

    batch_images.reserve(batch_size);
    batch_card_ids.reserve(batch_size);

    int processed_count = 0;
    std::cout << "Starting batch processing on: " << image_dir << "\n";

    auto process_current_batch = [&]() {
        if (batch_images.empty())
            return;

        std::vector<int> current_batch_slots;
        current_batch_slots.reserve(batch_card_ids.size());

        for (const auto &cid : batch_card_ids) {
            int slot = 0;
            auto it = card_id_to_slot.find(cid);
            if (it == card_id_to_slot.end()) {
                slot = static_cast<int>(new_card_ids.size());
                card_id_to_slot[cid] = slot;
                new_card_ids.push_back(cid);
            } else {
                slot = it->second;
            }
            current_batch_slots.push_back(slot);
        }

        std::vector<float> batch_vecs = core_.embed_batch(batch_images);

        all_embeddings.insert(all_embeddings.end(), batch_vecs.begin(), batch_vecs.end());
        new_row2card.insert(new_row2card.end(),
                            current_batch_slots.begin(),
                            current_batch_slots.end());

        processed_count += batch_images.size();
        std::cout << "Processed " << processed_count << " images...\n";

        batch_images.clear();
        batch_card_ids.clear();
    };

    for (const auto &entry : fs::recursive_directory_iterator(image_dir)) {
        if (cancelRequested_)
            throw std::runtime_error("Cancelled by user");

        if (!entry.is_regular_file())
            continue;

        std::string filepath = entry.path().string();
        std::string card_id = entry.path().stem().string();

        cv::Mat img = cv::imread(filepath, cv::IMREAD_COLOR);
        if (img.empty())
            continue;

        batch_images.push_back(img);
        batch_card_ids.push_back(card_id);

        if (batch_images.size() >= batch_size) {
            process_current_batch();
            if (cancelRequested_)
                throw std::runtime_error("Cancelled");
        }
    }

    process_current_batch();

    if (all_embeddings.empty()) {
        throw std::runtime_error("No valid images found or embedded.");
    }

    int out_dim = core_.out_dim();

    std::cout << "Building FAISS IndexFlatIP (dim=" << out_dim << ")...\n";

    faiss::IndexFlatIP new_index(out_dim);
    new_index.add(processed_count, all_embeddings.data());

    fs::create_directories(save_dir);
    std::string faiss_path = save_dir + "/index.faiss";
    std::string npy_path = save_dir + "/row2card.npy";
    std::string json_path = save_dir + "/id_map.json";

    faiss::write_index(&new_index, faiss_path.c_str());
    TcgUtil::write_npy_int(npy_path, new_row2card);
    TcgUtil::write_json_string_array(json_path, new_card_ids);

    // auto new_index =
    // std::unique_ptr<faiss::Index>(faiss::read_index(faiss_path.c_str()));

    // core_->ReloadIndex(std::move(new_index),
    //                     std::move(new_row2card),
    //                     std::move(new_card_ids));

    std::cout << "Successfully created batched index!\n";
}