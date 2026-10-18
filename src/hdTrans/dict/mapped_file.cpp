#include "hdTrans/dict/mapped_file.h"

#include <limits>
#include <new>
#include <string>
#include <utility>

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if !defined(__EMSCRIPTEN__)
#include <sys/mman.h>
#endif
#endif

namespace hdTrans::dict::internal {
namespace {

bool ValidPath(std::string_view utf8) {
    return !utf8.empty() && utf8.find('\0') == std::string_view::npos;
}

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)

bool WidePath(std::string_view utf8, std::wstring& path) {
    if (!ValidPath(utf8) || !std::in_range<int>(utf8.size())) return false;
    const int length = static_cast<int>(utf8.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         utf8.data(), length, nullptr, 0);
    if (count == 0 || static_cast<std::size_t>(count) > path.max_size()) return false;
    path.resize(static_cast<std::size_t>(count));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                               utf8.data(), length, path.data(), count) == count;
}

bool FileTimeNanoseconds(FILETIME time, int64_t& result) {
    constexpr uint64_t kEpochTicks = 116444736000000000ULL;
    constexpr uint64_t kMaxTicks = static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()) / 100;
    const uint64_t ticks = (uint64_t{time.dwHighDateTime} << 32) | time.dwLowDateTime;
    const bool negative = ticks < kEpochTicks;
    const uint64_t distance = negative ? kEpochTicks - ticks : ticks - kEpochTicks;
    if (distance > kMaxTicks) return false;
    const int64_t nanoseconds = static_cast<int64_t>(distance) * 100;
    result = negative ? -nanoseconds : nanoseconds;
    return true;
}

#else

bool NativePath(std::string_view utf8, std::string& path) {
    if (!ValidPath(utf8) || utf8.size() > path.max_size()) return false;
    path.assign(utf8);
    return true;
}

bool TimeNanoseconds(int64_t seconds, int64_t nanoseconds, int64_t& result) {
    constexpr int64_t kScale = 1000000000;
    constexpr int64_t kMin = std::numeric_limits<int64_t>::min();
    constexpr int64_t kMax = std::numeric_limits<int64_t>::max();
    constexpr int64_t kMinSeconds = kMin / kScale - 1;
    constexpr int64_t kMinNanoseconds = kScale + kMin % kScale;
    if (nanoseconds < 0 || nanoseconds >= kScale ||
        seconds < kMinSeconds || seconds > kMax / kScale) return false;
    if (seconds == kMinSeconds) {
        if (nanoseconds < kMinNanoseconds) return false;
        result = kMin + (nanoseconds - kMinNanoseconds);
    } else {
        const int64_t base = seconds * kScale;
        if (base > kMax - nanoseconds) return false;
        result = base + nanoseconds;
    }
    return true;
}

#endif

}  // namespace

std::shared_ptr<const MappedFile> MappedFile::Open(std::string_view utf8) {
    std::unique_ptr<MappedFile> mapped(new (std::nothrow) MappedFile);
    if (!mapped) return {};

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    std::wstring path;
    if (!WidePath(utf8, path)) return {};
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER length;
    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileSizeEx(file, &length) ||
        length.QuadPart <= 0 || !std::in_range<std::size_t>(length.QuadPart)) {
        CloseHandle(file);
        return {};
    }
    const std::size_t size = static_cast<std::size_t>(length.QuadPart);
    const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        CloseHandle(file);
        return {};
    }
    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, size);
    CloseHandle(mapping);
    CloseHandle(file);
    if (view == nullptr) return {};
    mapped->bytes_ = {static_cast<const std::byte*>(view), size};
#else
    std::string path;
    if (!NativePath(utf8, path)) return {};
    const int file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (file < 0) return {};
    struct stat info {};
    if (::fstat(file, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_size <= 0 || !std::in_range<std::size_t>(info.st_size)) {
        ::close(file);
        return {};
    }
    const std::size_t size = static_cast<std::size_t>(info.st_size);
#if defined(__EMSCRIPTEN__)
    if (size > mapped->storage_.max_size()) {
        ::close(file);
        return {};
    }
    mapped->storage_.resize(size);
    std::size_t offset = 0;
    while (offset < size) {
        constexpr std::size_t kChunk = 1 << 20;
        const std::size_t count = size - offset < kChunk ? size - offset : kChunk;
        const auto read = ::read(file, mapped->storage_.data() + offset, count);
        if (read < 0 && errno == EINTR) continue;
        if (read <= 0) {
            ::close(file);
            return {};
        }
        offset += static_cast<std::size_t>(read);
    }
    ::close(file);
    mapped->bytes_ = mapped->storage_;
#else
    void* view = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, file, 0);
    ::close(file);
    if (view == MAP_FAILED) return {};
    mapped->bytes_ = {static_cast<const std::byte*>(view), size};
#endif
#endif
    return std::shared_ptr<const MappedFile>(std::move(mapped));
}

MappedFile::~MappedFile() {
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if (!bytes_.empty()) UnmapViewOfFile(bytes_.data());
#elif !defined(__EMSCRIPTEN__)
    if (!bytes_.empty()) ::munmap(const_cast<std::byte*>(bytes_.data()), bytes_.size());
#endif
}

bool ReadStamp(std::string_view utf8, FileStamp& stamp) {
    FileStamp result;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    std::wstring path;
    if (!WidePath(utf8, path)) return false;
    const HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info {};
    const bool success = GetFileInformationByHandle(file, &info) != 0;
    CloseHandle(file);
    if (!success || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        !FileTimeNanoseconds(info.ftLastWriteTime, result.mtimeNs)) return false;
    result.size = (uint64_t{info.nFileSizeHigh} << 32) | info.nFileSizeLow;
    result.identityHigh = info.dwVolumeSerialNumber;
    result.identityLow = (uint64_t{info.nFileIndexHigh} << 32) | info.nFileIndexLow;
    result.platform = 2;
#else
    std::string path;
    if (!NativePath(utf8, path)) return false;
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) ||
        !std::in_range<uint64_t>(info.st_size)) return false;
#if defined(__APPLE__)
    const auto time = info.st_mtimespec;
#else
    const auto time = info.st_mtim;
#endif
    if (!std::in_range<int64_t>(time.tv_sec) || !std::in_range<int64_t>(time.tv_nsec) ||
        !TimeNanoseconds(static_cast<int64_t>(time.tv_sec),
                         static_cast<int64_t>(time.tv_nsec), result.mtimeNs)) return false;
    result.size = static_cast<uint64_t>(info.st_size);
    result.identityHigh = static_cast<uint64_t>(info.st_dev);
    result.identityLow = static_cast<uint64_t>(info.st_ino);
    result.platform = 1;
#endif
    stamp = result;
    return true;
}

}  // namespace hdTrans::dict::internal
