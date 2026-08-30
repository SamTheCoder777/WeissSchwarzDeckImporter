#pragma once

#include <QString>
#include <regex>
#include <string>

#include "tcg_core.h"

class IndexBuilder {
public:
    using ProgressFn = std::function<void(const QString &msg, int processed, int total)>;

    explicit IndexBuilder(TcgCore &core)
        : core_(core)
    {}

    void create_index_batched(const std::string &image_dir,
                              const std::string &save_dir,
                              int batch_size = 32,
                              bool disable_name_check = false,
                              ProgressFn progress = nullptr);

    void requestCancel() { cancelRequested_ = true; }
    void resetCancel() { cancelRequested_ = false; }

private:
  TcgCore &core_;

  static const inline std::regex set_pattern_{"\\w+(-)\\w+-.+"};

  std::atomic<bool> cancelRequested_{false};
};