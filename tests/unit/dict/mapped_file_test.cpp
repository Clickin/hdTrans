#include "hdTrans/dict/mapped_file.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <windows.h>
#include <winioctl.h>
#include <thread>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "hdtest.h"

namespace {

namespace fs = std::filesystem;
using namespace hdTrans::dict::internal;
using FilePtr = std::unique_ptr<std::FILE, decltype(&std::fclose)>;

struct CacheFolder {
    fs::path path;
    CacheFolder() {
        std::error_code ec;
        path = fs::temp_directory_path(ec) /
               ("hdtrans-cache-io-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path, ec);
    }
    ~CacheFolder() { std::error_code ec; fs::remove_all(path, ec); }
    fs::path File(const std::string& name) const { return path / name; }
};

std::string Key(char c) { return std::string(64, c); }
std::string Generation(char key, char token) { return Key(key) + "." + std::string(32, token) + ".hdsnap"; }

bool Write(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    file.close();
    return static_cast<bool>(file);
}

std::string Read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool Age(const fs::path& path, int minutes) {
    std::error_code ec;
    fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::minutes(minutes), ec);
    return !ec;
}

bool Sparse(const fs::path& path, std::uintmax_t size) {
    if (!Write(path, "")) return false;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD count = 0;
    const bool ok = DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &count, nullptr) != 0;
    CloseHandle(file);
    if (!ok) return false;
#endif
    std::error_code ec;
    fs::resize_file(path, size, ec);
    return !ec;
}

}  // namespace

HD_TEST(CacheSmallBoundedRegularFiles) {
    CacheFolder folder;
    const auto path = folder.File("small");
    for (std::size_t size : {0U, 1U, 105U, 511U, 512U, 1024U}) {
        const std::string content(size, 'x');
        HD_REQUIRE(Write(path, content));
        std::string out = "unchanged";
        HD_CHECK(ReadCacheSmall(path, out) == (size < 512));
        HD_CHECK(out == (size < 512 ? content : "unchanged"));
    }
    const std::string binary("a\0b", 3);
    HD_REQUIRE(Write(path, binary));
    std::string out;
    HD_CHECK(ReadCacheSmall(path, out) && out == binary);
    out = "unchanged";
    HD_CHECK(!ReadCacheSmall(folder.File("absent"), out) && out == "unchanged");
    HD_CHECK(!ReadCacheSmall(folder.path, out) && out == "unchanged");
    HD_CHECK(!ReadCacheSmall(folder.File(std::string("bad\0name", 8)), out) && out == "unchanged");
#if !defined(_WIN32) || defined(__EMSCRIPTEN__)
    const auto fifo = folder.File("fifo");
    HD_REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);
    HD_CHECK(!ReadCacheSmall(fifo, out) && out == "unchanged");
#endif
}

HD_TEST(CacheTempExclusivePublish) {
    CacheFolder folder;
    const auto target = folder.File(Key('a') + ".latest");
    HD_REQUIRE(Write(target, "old"));
    fs::path firstPath, secondPath;
    FilePtr first(OpenCacheTemp(target, firstPath), &std::fclose);
    FilePtr second(OpenCacheTemp(target, secondPath), &std::fclose);
    HD_REQUIRE(first && second);
    HD_CHECK(firstPath != secondPath);
    HD_CHECK(firstPath.parent_path() == target.parent_path());
    HD_CHECK(firstPath.filename().native().size() == target.filename().native().size() + 37);
    HD_CHECK(Read(target) == "old");
#if !defined(_WIN32) || defined(__EMSCRIPTEN__)
    struct stat info {};
    HD_REQUIRE(::fstat(::fileno(first.get()), &info) == 0);
    HD_CHECK((info.st_mode & 0777) == 0600);
    HD_CHECK((::fcntl(::fileno(first.get()), F_GETFD) & FD_CLOEXEC) != 0);
#endif
    HD_REQUIRE(std::fwrite("first", 1, 5, first.get()) == 5);
    HD_REQUIRE(std::fwrite("second", 1, 6, second.get()) == 6);
    HD_CHECK(CloseCacheTemp(first.release()));
    HD_CHECK(CloseCacheTemp(second.release()));
    HD_CHECK(PublishCacheTemp(firstPath, target));
    HD_CHECK(Read(target) == "first");
    HD_CHECK(!fs::exists(firstPath));
    HD_CHECK(PublishCacheTemp(secondPath, target));
    HD_CHECK(Read(target) == "second");
    HD_CHECK(!fs::exists(secondPath));
}

HD_TEST(CacheTempFailures) {
    CacheFolder folder;
    fs::path temp = "unchanged";
    HD_CHECK(OpenCacheTemp({}, temp) == nullptr);
    HD_CHECK(temp.empty());
    HD_CHECK(OpenCacheTemp(folder.File("absent") / "file", temp) == nullptr);
    HD_CHECK(temp.empty());
    HD_CHECK(OpenCacheTemp(folder.File(std::string("bad\0name", 8)), temp) == nullptr);
    HD_CHECK(temp.empty());
    HD_CHECK(!CloseCacheTemp(nullptr));
    HD_CHECK(!PublishCacheTemp(folder.File("absent"), folder.File("target")));
}

HD_TEST(CacheTempSymlinks) {
    CacheFolder folder;
    const auto victim = folder.File("victim");
    const auto target = folder.File(Key('a') + ".latest");
    HD_REQUIRE(Write(victim, "untouched"));
    std::error_code ec;
    fs::create_symlink(victim, target, ec);
    if (ec) {
#ifdef _WIN32
        HD_CHECK(ec.value() == ERROR_PRIVILEGE_NOT_HELD || ec == std::errc::permission_denied ||
                 ec == std::errc::operation_not_permitted || ec == std::errc::operation_not_supported);
#else
        HD_REQUIRE(!ec);
#endif
        return;
    }
    std::string out = "unchanged";
    HD_CHECK(!ReadCacheSmall(target, out) && out == "unchanged");
    const auto planted = folder.File(Key('a') + ".latest.tmp." + std::string(32, '1'));
    fs::create_symlink(victim, planted, ec);
    HD_REQUIRE(!ec);
    HD_REQUIRE(Age(victim, 20));
    const auto oldTime = fs::last_write_time(victim);
    MaintainCache(target, folder.File(Generation('a', '2')));
    HD_CHECK(fs::last_write_time(victim) == oldTime);
    HD_CHECK(fs::is_symlink(fs::symlink_status(planted)));
    fs::path temp;
    FilePtr file(OpenCacheTemp(target, temp), &std::fclose);
    HD_REQUIRE(file != nullptr);
    HD_REQUIRE(std::fwrite("new", 1, 3, file.get()) == 3);
    HD_CHECK(CloseCacheTemp(file.release()));
    HD_CHECK(Read(victim) == "untouched");
    HD_CHECK(!PublishCacheTemp(planted, target));
    HD_CHECK(PublishCacheTemp(temp, target));
    HD_CHECK(!fs::is_symlink(fs::symlink_status(target)));
    HD_CHECK(Read(target) == "new");
    const auto before = fs::last_write_time(victim);
    MaintainCache(target, folder.File(Generation('a', '2')));
    HD_CHECK(Read(victim) == "untouched");
    HD_CHECK(fs::last_write_time(victim) == before);
    HD_CHECK(fs::is_symlink(fs::symlink_status(planted)));
}

HD_TEST(CacheMaintainStaleAndLiveGenerations) {
    CacheFolder folder;
    const auto index = folder.File(Key('a') + ".latest");
    const auto active = folder.File(Generation('a', '1'));
    const auto referenced = folder.File(Generation('a', '2'));
    HD_REQUIRE(Write(index, referenced.filename().string() + "\n"));
    HD_REQUIRE(Age(index, 20));
    const auto before = fs::last_write_time(index);
    HD_REQUIRE(Write(active, "active") && Age(active, 20));
    HD_REQUIRE(Write(referenced, "referenced") && Age(referenced, 20));
    for (const auto& generation : {active, referenced}) {
        auto sidecar = generation;
        sidecar += ".meta";
        HD_REQUIRE(Write(sidecar, "live") && Age(sidecar, 20));
    }
    const std::string names[] = {
        Generation('a', '3'), Generation('a', '3') + ".meta", Generation('a', '3') + ".bad",
        Generation('a', '3') + ".quarantine", Key('a') + ".latest.tmp." + std::string(32, '4'),
        Generation('a', '1') + ".tmp." + std::string(32, '5'),
        Generation('a', '3') + ".tmp." + std::string(32, 'a') + ".bad",
        Generation('a', '3') + ".tmp." + std::string(32, 'b') + ".quarantine",
        Generation('a', '3') + ".meta.tmp." + std::string(32, 'c'),
        Key('b') + ".latest.tmp." + std::string(32, '6'), "unrelated.tmp.old",
        Key('a') + ".latest.tmp.nothex", Generation('b', '7'), Key('a') + ".meta",
        Key('a') + ".latest.meta", Generation('a', 'A'),
        Generation('a', '3') + ".tmp." + std::string(31, 'd') + ".bad",
    };
    for (const auto& name : names) HD_REQUIRE(Write(folder.File(name), "old") && Age(folder.File(name), 20));
    const auto fresh = folder.File(Key('a') + ".latest.tmp." + std::string(32, '8'));
    const auto freshGeneration = folder.File(Generation('a', '9'));
    HD_REQUIRE(Write(fresh, "fresh") && Write(freshGeneration, "fresh"));
    MaintainCache(index, active);
    HD_CHECK(fs::last_write_time(index) > before);
    HD_CHECK(fs::exists(active) && fs::exists(referenced));
    HD_CHECK(fs::exists(folder.File(Generation('a', '1') + ".meta")));
    HD_CHECK(fs::exists(folder.File(Generation('a', '2') + ".meta")));
    HD_CHECK(fs::exists(fresh) && fs::exists(freshGeneration));
    for (std::size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        HD_CHECK(fs::exists(folder.File(names[i])) == (i >= 9));
}

HD_TEST(CacheMaintainMalformedIndexPreservesGenerations) {
    const std::string contents[] = {
        Generation('a', '2'), Generation('a', '2') + "\r\n", Generation('b', '2') + "\n",
        Generation('a', 'A') + "\n", "../" + Generation('a', '2') + "\n",
    };
    for (const auto& content : contents) {
        CacheFolder folder;
        const auto index = folder.File(Key('a') + ".latest");
        const auto orphan = folder.File(Generation('a', '1'));
        HD_REQUIRE(Write(orphan, "keep") && Age(orphan, 20));
        HD_REQUIRE(Write(index, content));
        MaintainCache(index, folder.File(Generation('a', '3')));
        HD_CHECK(fs::exists(orphan));
    }
}

HD_TEST(CacheMaintainCapLru) {
    CacheFolder folder;
    constexpr std::uintmax_t mb = 1024 * 1024;
    const auto current = folder.File(Key('a') + ".latest");
    const auto active = folder.File(Generation('a', '1'));
    const auto old = folder.File(Key('b') + ".latest");
    const auto recent = folder.File(Key('c') + ".latest");
    HD_REQUIRE(Write(current, active.filename().string() + "\n"));
    HD_REQUIRE(Write(old, Generation('b', '2') + "\n") && Age(old, 60));
    HD_REQUIRE(Write(recent, Generation('c', '3') + "\n") && Age(recent, 20));
    HD_REQUIRE(Sparse(active, 220 * mb));
    HD_REQUIRE(Sparse(folder.File(Generation('b', '2')), 200 * mb) && Age(folder.File(Generation('b', '2')), 60));
    HD_REQUIRE(Sparse(folder.File(Generation('c', '3')), 200 * mb) && Age(folder.File(Generation('c', '3')), 20));
    const auto freshGeneration = folder.File(Generation('b', '9'));
    HD_REQUIRE(Sparse(freshGeneration, 32 * mb));
    HD_REQUIRE(Sparse(folder.File("unrelated.bin"), 768 * mb));
    HD_REQUIRE(Sparse(folder.File(Key('d') + ".meta"), 768 * mb));
    for (const auto* suffix : {".meta", ".bad", ".quarantine"})
        HD_REQUIRE(Write(folder.File(Generation('b', '2') + suffix), "sidecar") &&
                   Age(folder.File(Generation('b', '2') + suffix), 60));
    const auto temp = folder.File(Key('b') + ".latest.tmp." + std::string(32, '4'));
    HD_REQUIRE(Write(temp, "fresh"));
    MaintainCache(current, active);
    HD_CHECK(fs::exists(current) && fs::exists(active));
    HD_CHECK(!fs::exists(old) && !fs::exists(folder.File(Generation('b', '2'))));
    for (const auto* suffix : {".meta", ".bad", ".quarantine"})
        HD_CHECK(!fs::exists(folder.File(Generation('b', '2') + suffix)));
    HD_CHECK(fs::exists(recent) && fs::exists(folder.File(Generation('c', '3'))));
    HD_CHECK(fs::exists(temp) && fs::exists(freshGeneration) && fs::exists(folder.File("unrelated.bin")));
    HD_CHECK(fs::exists(folder.File(Key('d') + ".meta")));
}

HD_TEST(CacheMaintainCapProtectsCurrent) {
    CacheFolder folder;
    constexpr std::uintmax_t mb = 1024 * 1024;
    const auto index = folder.File(Key('a') + ".latest");
    const auto active = folder.File(Generation('a', '1'));
    const auto referenced = folder.File(Generation('a', '2'));
    HD_REQUIRE(Write(index, referenced.filename().string() + "\n"));
    HD_REQUIRE(Sparse(active, 300 * mb) && Age(active, 20));
    HD_REQUIRE(Sparse(referenced, 300 * mb) && Age(referenced, 20));
    const auto other = folder.File(Key('b') + ".latest");
    HD_REQUIRE(Write(other, Generation('b', '3') + "\n") && Age(other, 20));
    HD_REQUIRE(Write(folder.File(Generation('b', '3')), "other") && Age(folder.File(Generation('b', '3')), 20));
    const auto freshIndex = folder.File(Key('c') + ".latest");
    const auto freshGeneration = folder.File(Generation('c', '4'));
    HD_REQUIRE(Write(freshIndex, Generation('c', '4') + "\n") && Sparse(freshGeneration, 32 * mb));
    MaintainCache(index, active);
    HD_CHECK(fs::exists(index) && fs::exists(active) && fs::exists(referenced));
    HD_CHECK(!fs::exists(other) && !fs::exists(folder.File(Generation('b', '3'))));
    HD_CHECK(fs::exists(freshIndex) && fs::exists(freshGeneration));
}

HD_TEST(CacheMaintainRunsOncePerDirectory) {
    CacheFolder folder;
    const auto first = folder.File(Key('a') + ".latest");
    const auto active = folder.File(Generation('a', '1'));
    HD_REQUIRE(Write(first, active.filename().string() + "\n"));
    HD_REQUIRE(Write(active, "active"));
    MaintainCache(first, active);
    const auto second = folder.File(Key('b') + ".latest");
    const auto other = folder.File(Generation('b', '2'));
    const auto temp = folder.File(Key('b') + ".latest.tmp." + std::string(32, '3'));
    HD_REQUIRE(Write(second, other.filename().string() + "\n") && Age(second, 20));
    HD_REQUIRE(Write(other, "other"));
    HD_REQUIRE(Write(temp, "old") && Age(temp, 20));
    const auto before = fs::last_write_time(second);
    MaintainCache(second.parent_path() / "." / second.filename(), other);
    HD_CHECK(fs::last_write_time(second) == before);
    HD_CHECK(fs::exists(temp));
    CacheFolder separate;
    const auto index = separate.File(Key('c') + ".latest");
    const auto resource = separate.File(Generation('c', '4'));
    const auto stale = separate.File(Key('c') + ".latest.tmp." + std::string(32, '5'));
    HD_REQUIRE(Write(index, resource.filename().string() + "\n") && Write(resource, "current"));
    HD_REQUIRE(Write(stale, "old") && Age(stale, 20));
    MaintainCache(index, resource);
    HD_CHECK(!fs::exists(stale));
}

#ifdef _WIN32
HD_TEST(CachePublishRetriesSharingViolation) {
    CacheFolder folder;
    const auto target = folder.File("target");
    HD_REQUIRE(Write(target, "old"));
    fs::path temp;
    std::FILE* file = OpenCacheTemp(target, temp);
    HD_REQUIRE(file != nullptr);
    const bool wrote = std::fputs("new", file) >= 0;
    const bool flushed = CloseCacheTemp(file);
    HD_REQUIRE(wrote && flushed);
    const HANDLE locked = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HD_REQUIRE(locked != INVALID_HANDLE_VALUE);
    const bool blocked = MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0;
    const DWORD error = GetLastError();
    if (!blocked || error != ERROR_SHARING_VIOLATION) {
        CloseHandle(locked);
        HD_REQUIRE(blocked && error == ERROR_SHARING_VIOLATION);
    }
    bool closed = false;
    std::thread release([&] {
        Sleep(1);
        closed = CloseHandle(locked) != 0;
    });
    const bool published = PublishCacheTemp(temp, target);
    release.join();
    HD_CHECK(closed && published);
    HD_CHECK(Read(target) == "new");
    HD_CHECK(!fs::exists(temp));
}
#endif
