#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/dict/snapshot.h"

namespace hdTrans::dict::internal {

// The cache file selected by the ordered folder list.
std::string SnapshotPath(const std::vector<std::string>& folders, std::string_view cacheDir = {});

}  // namespace hdTrans::dict::internal
