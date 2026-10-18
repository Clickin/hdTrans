#include "hdTrans/dict/mapped_file.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <limits>
#include <mutex>
#include <new>
#include <unordered_set>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <windows.h>
#include <bcrypt.h>
#include <fcntl.h>
#include <io.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <fcntl.h>
#include <sys/random.h>
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

bool CachePath(const std::filesystem::path& path) {
    return !path.empty() && path.has_filename() && path.filename() != "." && path.filename() != ".." &&
           path.native().find(std::filesystem::path::value_type{}) == std::filesystem::path::string_type::npos;
}

bool LowerHex(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool GenerationName(std::string_view name) {
    return name.size() == 104 && LowerHex(name.substr(0, 64)) && name[64] == '.' &&
           LowerHex(name.substr(65, 32)) && name.substr(97) == ".hdsnap";
}

std::string CacheFilename(const std::filesystem::path& path) {
    const auto name = path.filename().native();
    std::string out;
    for (auto c : name) {
        if (static_cast<std::uint32_t>(c) == 0 || static_cast<std::uint32_t>(c) > 127) return {};
        out.push_back(static_cast<char>(c));
    }
    return out;
}

struct CacheFile {
    std::filesystem::path path;
    std::string owner;
    std::string referenced;
    std::filesystem::file_time_type modified;
    std::uintmax_t size = 0;
    bool index = false;
    bool temp = false;
};

bool CacheName(std::string_view name, CacheFile& file) {
    if (name.size() < 65 || !LowerHex(name.substr(0, 64))) return false;
    const auto stripSidecar = [&name]() {
        for (std::string_view suffix : {".meta", ".bad", ".quarantine"}) {
            if (!name.ends_with(suffix)) continue;
            name.remove_suffix(suffix.size());
            return true;
        }
        return false;
    };
    const bool sidecar = stripSidecar();
    const auto tmp = name.rfind(".tmp.");
    if (tmp != std::string_view::npos) {
        if (name.size() - tmp != 37 || !LowerHex(name.substr(tmp + 5))) return false;
        file.temp = true;
        name = name.substr(0, tmp);
        stripSidecar();
    }
    if (!GenerationName(name) && (name.substr(64) != ".latest" || (sidecar && !file.temp))) return false;
    file.owner = name;
    file.index = !file.temp && !sidecar && name.substr(64) == ".latest";
    return true;
}

bool RandomToken(std::string& token) {
    unsigned char bytes[16];
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return false;
#else
    int result;
    do { result = ::getentropy(bytes, sizeof(bytes)); } while (result != 0 && errno == EINTR);
    if (result != 0) return false;
#endif
    constexpr char hex[] = "0123456789abcdef";
    token.clear();
    for (unsigned char byte : bytes) {
        token += hex[byte >> 4];
        token += hex[byte & 15];
    }
    return true;
}

bool FirstMaintenance(const std::filesystem::path& directory) {
    std::error_code ec;
    auto key = std::filesystem::weakly_canonical(directory, ec);
    if (ec) return false;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    const auto& text = key.native();
    if (!std::in_range<int>(text.size())) return false;
    const int size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr, 0);
    if (!size) return false;
    std::wstring folded(static_cast<std::size_t>(size), L'\0');
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(),
                      static_cast<int>(text.size()), folded.data(), size, nullptr, nullptr, 0) != size) return false;
    key = std::move(folded);
#endif
    static std::mutex mutex;
    static std::unordered_set<std::filesystem::path> directories;
    std::lock_guard lock(mutex);
    return directories.insert(std::move(key)).second;
}

void TouchIndex(const std::filesystem::path& path) {
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    const HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    BY_HANDLE_FILE_INFORMATION info {};
    if (GetFileInformationByHandle(file, &info) && GetFileType(file) == FILE_TYPE_DISK &&
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        SetFileTime(file, nullptr, nullptr, &now);
    }
    CloseHandle(file);
#else
    const int file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (file < 0) return;
    struct stat info {};
    if (::fstat(file, &info) == 0 && S_ISREG(info.st_mode)) ::futimens(file, nullptr);
    ::close(file);
#endif
}

std::string IndexedGeneration(const std::filesystem::path& path) {
    std::string bytes;
    if (!ReadCacheSmall(path, bytes) || bytes.size() != 105 || bytes[104] != '\n') return {};
    const std::string_view name(bytes.data(), 104);
    const auto index = CacheFilename(path);
    if (!GenerationName(name) || index.substr(0, 64) != name.substr(0, 64)) return {};
    return std::string(name);
}

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

bool ReadCacheSmall(const std::filesystem::path& path, std::string& out) {
    if (!CachePath(path)) return false;
    char bytes[511];
    std::size_t length = 0;
    bool ok = false;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info {};
    if (GetFileInformationByHandle(file, &info) && GetFileType(file) == FILE_TYPE_DISK &&
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0 &&
        info.nFileSizeHigh == 0 && info.nFileSizeLow < 512) {
        length = info.nFileSizeLow;
        std::size_t offset = 0;
        while (offset < length) {
            DWORD count = 0;
            if (!ReadFile(file, bytes + offset, static_cast<DWORD>(length - offset), &count, nullptr) || count == 0) break;
            offset += count;
        }
        if (offset == length) {
            char extra;
            DWORD count = 0;
            ok = ReadFile(file, &extra, 1, &count, nullptr) && count == 0;
        }
    }
    CloseHandle(file);
#else
    const int file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (file < 0) return false;
    struct stat info {};
    if (::fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0 && info.st_size < 512) {
        length = static_cast<std::size_t>(info.st_size);
        std::size_t offset = 0;
        while (offset < length) {
            const auto count = ::read(file, bytes + offset, length - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            offset += static_cast<std::size_t>(count);
        }
        if (offset == length) {
            char extra;
            ssize_t count;
            do { count = ::read(file, &extra, 1); } while (count < 0 && errno == EINTR);
            ok = count == 0;
        }
    }
    ::close(file);
#endif
    if (ok) out.assign(bytes, length);
    return ok;
}

std::FILE* OpenCacheTemp(const std::filesystem::path& target, std::filesystem::path& temp) {
    temp.clear();
    if (!CachePath(target)) return nullptr;
    for (int attempt = 0; attempt < 16; attempt++) {
        std::string token;
        if (!RandomToken(token)) return nullptr;
        auto candidate = target;
        candidate += ".tmp." + token;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
        const HANDLE handle = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
            return nullptr;
        }
        const int fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(handle), _O_WRONLY | _O_BINARY | _O_NOINHERIT);
        if (fd < 0) {
            CloseHandle(handle);
            DeleteFileW(candidate.c_str());
            return nullptr;
        }
        std::FILE* file = _fdopen(fd, "wb");
        if (!file) {
            _close(fd);
            DeleteFileW(candidate.c_str());
            return nullptr;
        }
#else
        const int fd = ::open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            return nullptr;
        }
        std::FILE* file = ::fdopen(fd, "wb");
        if (!file) {
            ::close(fd);
            ::unlink(candidate.c_str());
            return nullptr;
        }
#endif
        temp = std::move(candidate);
        return file;
    }
    return nullptr;
}

bool CloseCacheTemp(std::FILE* file) {
    if (!file) return false;
    bool ok = std::fflush(file) == 0;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file)));
    if (handle == INVALID_HANDLE_VALUE || !FlushFileBuffers(handle)) ok = false;
#else
    int result;
    do { result = ::fsync(::fileno(file)); } while (result != 0 && errno == EINTR);
    if (result != 0) ok = false;
#endif
    if (std::fclose(file) != 0) ok = false;
    return ok;
}

bool PublishCacheTemp(const std::filesystem::path& from, const std::filesystem::path& to) {
    if (!CachePath(from) || !CachePath(to) || from.parent_path() != to.parent_path()) return false;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    const DWORD attributes = GetFileAttributesW(from.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0) return false;
    for (int attempt = 0; attempt < 4; attempt++) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        if (GetLastError() != ERROR_SHARING_VIOLATION) return false;
        Sleep(1);
    }
    return false;
#else
    const auto parent = to.has_parent_path() ? to.parent_path() : std::filesystem::path(".");
    const int dir = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dir < 0) return false;
    struct stat info {};
    bool ok = ::fstatat(dir, from.filename().c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(info.st_mode);
    if (ok) ok = ::fsync(dir) == 0;
    if (ok) ok = ::renameat(dir, from.filename().c_str(), dir, to.filename().c_str()) == 0;
    if (ok) ok = ::fsync(dir) == 0;
    ::close(dir);
    return ok;
#endif
}

void MaintainCache(const std::filesystem::path& index, const std::filesystem::path& activeGeneration) {
    const auto indexName = CacheFilename(index);
    CacheFile current;
    if (!CachePath(index) || !CacheName(indexName, current) || !current.index) return;
    const auto key = indexName.substr(0, 64);
    const auto activeName = CacheFilename(activeGeneration);
    const auto activeKey = GenerationName(activeName) ? activeName.substr(0, 64) : key;
    const auto directory = index.has_parent_path() ? index.parent_path() : std::filesystem::path(".");
    if (!FirstMaintenance(directory)) return;
    TouchIndex(index);
    const auto referenced = IndexedGeneration(index);
    const auto stale = std::filesystem::file_time_type::clock::now() - std::chrono::minutes(10);
    std::vector<CacheFile> files;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        CacheFile file;
        if (!CacheName(CacheFilename(it->path()), file)) continue;
        std::error_code error;
        if (!std::filesystem::is_regular_file(it->symlink_status(error)) || error) continue;
        file.path = it->path();
        file.modified = it->last_write_time(error);
        if (error) continue;
        file.size = it->file_size(error);
        if (error) continue;
        if (file.index) file.referenced = IndexedGeneration(file.path);
        const bool sameKey = file.owner.substr(0, 64) == key;
        const bool orphan = !referenced.empty() && GenerationName(file.owner) &&
                            file.owner != activeName && file.owner != referenced;
        if (sameKey && file.modified < stale && (file.temp || orphan)) {
            const auto live = file.temp ? std::string() : IndexedGeneration(index);
            if ((file.temp || (!live.empty() && file.owner != live)) &&
                std::filesystem::remove(file.path, error)) continue;
        }
        files.push_back(std::move(file));
    }
    std::uintmax_t total = 0;
    std::vector<std::size_t> indexes;
    for (std::size_t i = 0; i < files.size(); i++) {
        const auto& file = files[i];
        total += (std::min)(file.size, (std::numeric_limits<std::uintmax_t>::max)() - total);
        const auto fileKey = file.owner.substr(0, 64);
        if (file.index && !file.referenced.empty() && file.modified < stale &&
            fileKey != key && fileKey != activeKey) indexes.push_back(i);
    }
    std::sort(indexes.begin(), indexes.end(), [&files](std::size_t a, std::size_t b) {
        return files[a].modified < files[b].modified;
    });
    constexpr std::uintmax_t kLimit = 512ULL * 1024 * 1024;
    for (const auto i : indexes) {
        if (total <= kLimit) break;
        auto& victim = files[i];
        const auto victimKey = victim.owner.substr(0, 64);
        ec.clear();
        const auto modified = std::filesystem::last_write_time(victim.path, ec);
        if (ec || modified != victim.modified || IndexedGeneration(victim.path) != victim.referenced) continue;
        if (!std::filesystem::remove(victim.path, ec) && ec) continue;
        total -= (std::min)(total, victim.size);
        victim.size = 0;
        for (auto& file : files) {
            if (file.index || file.temp || file.modified >= stale || file.owner.substr(0, 64) != victimKey) continue;
            ec.clear();
            if (std::filesystem::exists(std::filesystem::symlink_status(victim.path, ec)) ||
                (ec && ec != std::errc::no_such_file_or_directory)) break;
            if (!std::filesystem::remove(file.path, ec)) continue;
            total -= (std::min)(total, file.size);
            file.size = 0;
        }
    }
}

}  // namespace hdTrans::dict::internal
