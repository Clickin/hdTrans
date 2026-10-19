#include "core/hdtrans.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif

#include "core/dict/folders.h"
#include "core/engine.h"

namespace {

std::mutex g_mutex;
std::condition_variable g_adminsDone;
std::atomic<unsigned> g_waitingAdmins{0};
std::uint64_t g_generation = 0;
std::vector<std::string> g_dirs;
std::string g_userFile;
std::string g_ehnd;
std::unique_ptr<hdTrans::Engine> g_engine;
std::unique_ptr<hdTrans::Session> g_session;

// Queued state changes run before new translations.
class AdminLock {
public:
    AdminLock() : lock_(g_mutex, std::defer_lock) {
        ++g_waitingAdmins;
        lock_.lock();
    }
    ~AdminLock() {
        --g_waitingAdmins;
        lock_.unlock();
        g_adminsDone.notify_all();
    }

private:
    std::unique_lock<std::mutex> lock_;
};

std::vector<std::string> SplitNul(const char* dirs) {
    std::vector<std::string> out;
    for (const char* p = dirs; p && *p; p += std::strlen(p) + 1) out.emplace_back(p);
    return out;
}

// The folder of the module holding this library in UTF-8; empty when unknown.
std::string ModuleDir() {
#if defined(__EMSCRIPTEN__)
    return {};
#else
#if defined(_WIN32)
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ModuleDir), &self)) {
        return {};
    }
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(self, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    const std::filesystem::path image(path);
#else
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&ModuleDir), &info) || !info.dli_fname) return {};
    std::error_code ec;
    const std::filesystem::path image = std::filesystem::canonical(info.dli_fname, ec);
    if (ec) return {};
#endif
    const std::u8string dir = image.parent_path().u8string();
    return std::string(reinterpret_cast<const char*>(dir.data()), dir.size());
#endif
}

int Copy(const std::string& s, char** out, size_t* outn) {
    auto* buf = static_cast<char*>(std::malloc(s.size() + 1));
    if (!buf) return -1;
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = 0;
    *out = buf;
    *outn = s.size();
    return 0;
}

}  // namespace

#if defined(_WIN32) && defined(HDTRANS_BUILD_DLL)
// Nothing waits under the loader lock: at process exit (reserved set) the engine is left to
// the OS; on FreeLibrary no reload thread runs here any more (each holds a module reference)
// and the engine is released without waiting for one.
BOOL WINAPI DllMain(HINSTANCE /*module*/, DWORD reason, LPVOID reserved) {
    if (reason != DLL_PROCESS_DETACH) return TRUE;
    if (reserved) {
        static_cast<void>(g_session.release());
        static_cast<void>(g_engine.release());
        return TRUE;
    }
    g_session.reset();
    if (g_engine) g_engine->DetachReload();
    g_engine.reset();
    return TRUE;
}
#endif

extern "C" {

int hdTrans_Initialize(const char* dirs) {
    std::uint64_t generation;
    {
        AdminLock lock;
        generation = g_generation;
    }
    auto paths = SplitNul(dirs);
    const std::string dir = ModuleDir();
    std::string userFile = hdTrans::dict::UserFile(dir);
    std::string ehnd = hdTrans::dict::EhndFolder(dir);
    auto e = std::make_unique<hdTrans::Engine>(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Auto);
    e->OpenEhnd(ehnd);
    if (!e->Open(paths, false, userFile)) return -1;
    AdminLock lock;
    if (generation != g_generation) return -1;
    g_session.reset();
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine);
    g_dirs = std::move(paths);
    g_userFile = std::move(userFile);
    g_ehnd = std::move(ehnd);
    ++g_generation;
    return 0;
}

void hdTrans_Terminate(void) {
    AdminLock lock;
    ++g_generation;
    g_session.reset();
    g_engine.reset();
    g_dirs.clear();
    g_userFile.clear();
    g_ehnd.clear();
}

int hdTrans_Translate(const char* text, size_t n, char** out, size_t* outn) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_session) return -1;
    return Copy(g_session->Translate(std::string_view(text, n)), out, outn);
}

int hdTrans_Reload(void) {
    std::uint64_t generation;
    std::vector<std::string> dirs;
    std::string userFile, ehnd;
    {
        AdminLock lock;
        if (!g_engine) return -1;
        generation = g_generation;
        dirs = g_dirs;
        userFile = g_userFile;
        ehnd = g_ehnd;
    }
    auto e = std::make_unique<hdTrans::Engine>(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Auto);
    e->OpenEhnd(ehnd);
    // A userdict.tsv that does not load keeps the dictionaries loaded before.
    if (!e->Open(dirs, false, userFile) || !e->UserFileError().empty()) return -1;
    AdminLock lock;
    if (generation != g_generation) return -1;
    g_session.reset();
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine);
    ++g_generation;
    return 0;
}

int hdTrans_UserDictError(char** out, size_t* outn) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_engine) return -1;
    return Copy(g_engine->UserFileError(), out, outn);
}

int hdTrans_OpenEhnd(const char* dir) {
    AdminLock lock;
    if (!g_engine) return -1;
    const std::string d = dir ? dir : "";
    if (!g_engine->OpenEhnd(d)) return -1;
    g_ehnd = d;
    return 0;
}

int hdTrans_FilterErrors(char** out, size_t* outn) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_engine) return -1;
    std::string s;
    for (const std::string& e : g_engine->FilterErrors()) s += (s.empty() ? "" : "\n") + e;
    return Copy(s, out, outn);
}

void hdTrans_FreeMem(void* p) { std::free(p); }

const char* hdTrans_Version(void) { return "0.0.0"; }

}  // extern "C"
