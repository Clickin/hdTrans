#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"

namespace hdTrans::dict::internal {

// Reads through the first nonblank, noncomment physical line; false on an I/O error.
// Only a BOM on the first physical line is stripped; the first raw tab field is key.
bool ReadColumnHeader(std::string_view utf8Path, bool& column);

// Hashes the ordered canonical folders and data version.
// An empty cacheDir selects HDTRANS_CACHE_DIR or the per-user cache directory.
// An empty, relative or case-insensitive "off" HDTRANS_CACHE_DIR disables even an explicit cacheDir.
std::string SnapshotPath(const std::vector<std::string>& folders, std::string_view cacheDir = {});

bool RelativeCacheOverride();

// Resolves an atomic cache index to an immutable generation; empty on invalid input.
std::string SnapshotResourcePath(std::string_view index);
// Compiles and publishes a new cache generation and its index.
bool CompileCache(const std::vector<std::string>& folders, std::string_view cacheDir = {});

}  // namespace hdTrans::dict::internal
