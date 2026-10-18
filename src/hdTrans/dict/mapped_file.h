#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#if defined(__EMSCRIPTEN__)
#include <vector>
#endif

namespace hdTrans::dict::internal {

class MappedFile {
public:
    // Native views can reflect writes; the file must not be truncated while mapped.
    static std::shared_ptr<const MappedFile> Open(std::string_view utf8);
    std::span<const std::byte> Bytes() const { return bytes_; }
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

private:
    MappedFile() = default;

    std::span<const std::byte> bytes_;
#if defined(__EMSCRIPTEN__)
    std::vector<std::byte> storage_;
#endif
};

struct FileStamp {
    uint64_t size = 0;
    int64_t mtimeNs = 0;
    uint64_t identityHigh = 0;
    uint64_t identityLow = 0;
    uint32_t platform = 0;  // 1: POSIX device/inode; 2: Windows volume/file index.
    bool operator==(const FileStamp&) const = default;
};

// Leaves stamp unchanged on failure. Empty regular files have valid stamps.
bool ReadStamp(std::string_view utf8, FileStamp& stamp);

// Reads a regular file smaller than 512 bytes. Leaves out unchanged on failure.
bool ReadCacheSmall(const std::filesystem::path& path, std::string& out);

// Creates a private, exclusive sibling of target. Close flushes and closes even on failure.
std::FILE* OpenCacheTemp(const std::filesystem::path& target, std::filesystem::path& temp);
bool CloseCacheTemp(std::FILE* file);

// Atomically replaces to with a sibling from and synchronizes the directory entry.
bool PublishCacheTemp(const std::filesystem::path& from, const std::filesystem::path& to);

// Runs at most once per process per cache directory. The 512 MiB cap is best-effort:
// active generations and files younger than 10 minutes can exceed it.
// The current index and its referenced generation remain protected.
void MaintainCache(const std::filesystem::path& index, const std::filesystem::path& activeGeneration);

}  // namespace hdTrans::dict::internal
