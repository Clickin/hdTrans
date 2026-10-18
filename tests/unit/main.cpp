// Unit test runner. Usage: hdtrans_tests [name-substring-filter]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

#ifdef _WIN32
#include <windows.h>
#endif

#include "hdtest.h"

namespace {

namespace fs = std::filesystem;
using CacheListing = std::map<fs::path, std::pair<fs::file_type, fs::file_time_type>>;

fs::path EnvironmentPath(const char* name) {
#ifdef _WIN32
    const std::wstring wideName(name, name + std::strlen(name));
    const DWORD size = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(wideName.c_str(), value.data(), size);
    if (written == 0 || written >= size) return {};
    value.resize(written);
    return fs::path(value);
#else
    const char* value = std::getenv(name);
    return value == nullptr ? fs::path{} : fs::path(value);
#endif
}

fs::path UserCache() {
#ifdef _WIN32
    const auto base = EnvironmentPath("LOCALAPPDATA");
    return base.empty() ? fs::path{} : base / "hdtrans" / "cache";
#elif defined(__APPLE__)
    const auto home = EnvironmentPath("HOME");
    return home.empty() ? fs::path{} : home / "Library" / "Caches" / "hdtrans";
#else
    const auto xdg = EnvironmentPath("XDG_CACHE_HOME");
    if (!xdg.empty()) return xdg / "hdtrans";
    const auto home = EnvironmentPath("HOME");
    return home.empty() ? fs::path{} : home / ".cache" / "hdtrans";
#endif
}

bool ListCache(const fs::path& root, CacheListing& out) {
    if (root.empty()) return false;
    std::error_code ec;
    if (!fs::exists(root, ec)) return !ec;
    auto add = [&](const fs::path& path) {
        const auto type = fs::symlink_status(path, ec).type();
        if (ec) return false;
        const auto time = fs::last_write_time(path, ec);
        if (ec) return false;
        out.emplace(path.lexically_relative(root), std::pair{type, time});
        return true;
    };
    if (!add(root)) return false;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!add(it->path())) return false;
    }
    return !ec;
}

struct CacheGuard {
    fs::path real = UserCache();
    fs::path temporary;
    CacheListing before;
    bool initialized = false;
    bool watchReal = true;

    bool Init() {
        const auto requested = EnvironmentPath("HDTRANS_CACHE_DIR");
        if (!requested.empty() && !real.empty()) {
            std::error_code ec;
            const auto canonicalReal = fs::weakly_canonical(real, ec);
            if (ec) return false;
            const auto canonicalRequested = fs::weakly_canonical(requested, ec);
            if (ec) return false;
            watchReal = canonicalReal != canonicalRequested;
        }
        if (watchReal && !ListCache(real, before)) return false;
#ifdef _WIN32
        if (_wputenv_s(L"HDTRANS_SNAPSHOT", L"") != 0 || _wputenv_s(L"HDTRANS_VERIFY_DICTS", L"") != 0) return false;
        SetLastError(ERROR_SUCCESS);
        GetEnvironmentVariableW(L"HDTRANS_CACHE_DIR", nullptr, 0);
        const bool supplied = GetLastError() != ERROR_ENVVAR_NOT_FOUND;
#else
        if (unsetenv("HDTRANS_SNAPSHOT") != 0 || unsetenv("HDTRANS_VERIFY_DICTS") != 0) return false;
        const bool supplied = std::getenv("HDTRANS_CACHE_DIR") != nullptr;
#endif
        if (!supplied) {
            std::error_code ec;
            const auto base = fs::temp_directory_path(ec);
            if (ec) return false;
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (int i = 0; i < 100; ++i) {
                const auto candidate = base / ("hdtrans-tests-" + std::to_string(stamp) + "-" + std::to_string(i));
                if (fs::create_directory(candidate, ec)) { temporary = candidate; break; }
                if (ec) return false;
            }
            if (temporary.empty()) return false;
#ifndef _WIN32
            fs::permissions(temporary, fs::perms::owner_all, fs::perm_options::replace, ec);
            if (ec) return false;
#endif
#ifdef _WIN32
            if (_wputenv_s(L"HDTRANS_CACHE_DIR", temporary.c_str()) != 0) return false;
#else
            if (setenv("HDTRANS_CACHE_DIR", temporary.c_str(), 1) != 0) return false;
#endif
        }
        initialized = true;
        return true;
    }

    bool Unchanged() const {
        if (!watchReal) return true;
        CacheListing after;
        return ListCache(real, after) && after == before;
    }

    ~CacheGuard() {
        bool ok = !initialized || Unchanged();
        if (!temporary.empty()) {
#ifdef _WIN32
            ok = _wputenv_s(L"HDTRANS_CACHE_DIR", L"") == 0 && ok;
#else
            ok = unsetenv("HDTRANS_CACHE_DIR") == 0 && ok;
#endif
            std::error_code ec;
            fs::remove_all(temporary, ec);
            ok = !ec && ok;
        }
        if (!ok) {
            std::fprintf(stderr, "FAIL runner cache isolation or cleanup\n");
            std::fflush(nullptr);
            std::_Exit(1);
        }
    }
};

CacheGuard* cacheGuard = nullptr;
void CleanupCache() { delete cacheGuard; }

}  // namespace

int main(int argc, char** argv) {
    cacheGuard = new CacheGuard;
    // Register before tests create static snapshots, so mapped files close before cleanup.
    if (std::atexit(CleanupCache) != 0 || !cacheGuard->Init()) {
        std::fprintf(stderr, "FAIL runner cache initialization\n");
        return 1;
    }
    const char* filter = argc > 1 ? argv[1] : nullptr;
    const bool requireAll = hdtest::RequireAll();
    int run = 0, failed = 0, skipped = 0;
    for (const auto& t : hdtest::Registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        hdtest::Cur() = {};
        t.fn();
        run++;
        if (hdtest::Cur().skipped) {
            skipped++;
            std::printf("SKIP %s: %s\n", t.name, hdtest::Cur().skipReason.c_str());
        }
        if (hdtest::Cur().failures) {
            failed++;
            std::printf("FAIL %s\n", t.name);
        } else if (!hdtest::Cur().skipped) {
            std::printf("ok   %s\n", t.name);
        }
    }
    if (!cacheGuard->Unchanged()) {
        failed++;
        std::fprintf(stderr, "FAIL runner: per-user cache changed during the run\n");
    }
    if (run == 0) std::fprintf(stderr, "No tests matched%s%s\n", filter ? ": " : "", filter ? filter : "");
    std::printf("%d run, %d failed, %d skipped\n", run, failed, skipped);
    return failed || run == 0 || (requireAll && skipped) ? 1 : 0;
}
