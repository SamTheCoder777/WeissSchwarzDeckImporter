#pragma once

#include <string>

#include "tcg_core.h"

class IndexBuilder {
public:
  explicit IndexBuilder(TcgCore &core) : core_(core) {}

  void create_index_batched(const std::string &image_dir,
                            const std::string &save_dir, int batch_size = 32);

private:
  TcgCore &core_;
};