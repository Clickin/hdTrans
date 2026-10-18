#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/dict/snapshot.h"

namespace hdTrans::dict::internal {

// Hashes the ordered absolute, lexically normalized folders and data version.
// An empty cacheDir selects HDTRANS_CACHE_DIR or the per-user cache directory.
// A present but empty HDTRANS_CACHE_DIR disables caching even with an explicit cacheDir.
std::string SnapshotPath(const std::vector<std::string>& folders, std::string_view cacheDir = {});

}  // namespace hdTrans::dict::internal
