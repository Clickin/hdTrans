#include "core/engine/user_watch.h"

#include <new>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace hdTrans::internal {

namespace {

constinit std::atomic<int> g_running{0};

#if defined(_WIN32)

struct ThreadStart {
    std::function<void()> body;
    HMODULE module = nullptr;
};

DWORD WINAPI RunThread(void* p) {
    HMODULE module = nullptr;
    {
        std::unique_ptr<ThreadStart> start(static_cast<ThreadStart*>(p));
        start->body();
        module = start->module;
    }
    g_running.fetch_sub(1);
    if (module) FreeLibraryAndExitThread(module, 0);
    return 0;
}

#endif

}  // namespace

FileVersion VersionOf(std::string_view utf8Path) {
    FileVersion v;
    v.present = !utf8Path.empty() && dict::internal::ReadStamp(utf8Path, v.stamp);
    if (!v.present) v.stamp = {};
    return v;
}

bool FileWatch::Due(std::string_view path, std::chrono::steady_clock::time_point now,
                    std::chrono::steady_clock::duration interval, FileVersion& found) {
    if (now < next) return false;
    next = now + interval;
    found = VersionOf(path);
    return found != loaded && !(hasFailed && found == failed);
}

int RunningReloads() { return g_running.load(); }

#if defined(_WIN32)

bool ReloadThread::Start(std::function<void()> body) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&RunThread), &module)) {
        module = nullptr;
    }
    auto* start = new (std::nothrow) ThreadStart{std::move(body), module};
    if (start) {
        g_running.fetch_add(1);
        handle_ = CreateThread(nullptr, 0, RunThread, start, 0, nullptr);
        if (handle_) return true;
        g_running.fetch_sub(1);
        delete start;
    }
    if (module) FreeLibrary(module);
    return false;
}

bool ReloadThread::Joinable() const { return handle_ != nullptr; }

void ReloadThread::Join() {
    if (!handle_) return;
    WaitForSingleObject(handle_, INFINITE);
    CloseHandle(handle_);
    handle_ = nullptr;
}

void ReloadThread::Detach() {
    if (!handle_) return;
    CloseHandle(handle_);
    handle_ = nullptr;
}

#else

bool ReloadThread::Start(std::function<void()> body) {
    g_running.fetch_add(1);
    thread_ = std::thread([body = std::move(body)]() mutable {
        body();
        body = nullptr;
        g_running.fetch_sub(1);
    });
    return true;
}

bool ReloadThread::Joinable() const { return thread_.joinable(); }

void ReloadThread::Join() {
    if (thread_.joinable()) thread_.join();
}

void ReloadThread::Detach() {
    if (thread_.joinable()) thread_.detach();
}

#endif

}  // namespace hdTrans::internal
