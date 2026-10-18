#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
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

}  // namespace hdTrans::dict::internal
