// J2KEngine-compatible exports: Shift-JIS or UTF-16 in, CP949 or UTF-16 out.
// Dictionaries are found next to the DLL; the path the host passes is ignored.
// Calls are thread-safe, not reentrant; finish all calls before unloading the DLL.
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hdTrans/engine.h"

namespace {

std::string Narrow(UINT codepage, const wchar_t* s, int n) {
    if (n == 0) return {};
    const int len = WideCharToMultiByte(codepage, 0, s, n, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(codepage, 0, s, n, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring Widen(UINT codepage, const char* s, int n) {
    if (n == 0) return {};
    const int len = MultiByteToWideChar(codepage, 0, s, n, nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(codepage, 0, s, n, out.data(), len);
    return out;
}

std::string Utf8FromWide(const std::wstring& w) { return Narrow(CP_UTF8, w.data(), static_cast<int>(w.size())); }
std::wstring WideFromUtf8(const std::string& s) { return Widen(CP_UTF8, s.data(), static_cast<int>(s.size())); }

constexpr UINT kShiftJis = 932;
constexpr UINT kCp949 = 949;

std::mutex g_mutex;
std::condition_variable g_adminsDone;
std::atomic<unsigned> g_waitingAdmins{0};
std::uint64_t g_generation = 0;
std::vector<std::string> g_dirs;
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

std::string TranslateUtf8(const std::string& text) { return g_session ? g_session->Translate(text) : std::string(); }

// Copies s into a NUL-terminated block the host releases with J2K_FreeMem.
template <class Char>
Char* Block(const std::basic_string<Char>& s) {
    auto* p = static_cast<Char*>(HeapAlloc(GetProcessHeap(), 0, (s.size() + 1) * sizeof(Char)));
    if (!p) return nullptr;
    std::memcpy(p, s.data(), s.size() * sizeof(Char));
    p[s.size()] = 0;
    return p;
}

// <dll dir>\dicts\base, then <dll dir>\dicts\user when present.
std::vector<std::string> DictDirs() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&DictDirs), &self);
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring dir(path, n);
    const size_t slash = dir.find_last_of(L"\\/");
    dir = slash == std::wstring::npos ? L"." : dir.substr(0, slash);
    std::vector<std::string> dirs{Utf8FromWide(dir + L"\\dicts\\base")};
    const std::wstring user = dir + L"\\dicts\\user";
    if (GetFileAttributesW(user.c_str()) != INVALID_FILE_ATTRIBUTES) dirs.push_back(Utf8FromWide(user));
    return dirs;
}

}  // namespace

extern "C" {

// Terminate, Initialize or Reload can cancel a pending load (returns 0).
int __stdcall J2K_Initialize(char* /*dicPath*/) {
    std::uint64_t generation;
    {
        AdminLock lock;
        if (g_engine) return 1;
        generation = g_generation;
    }
    auto dirs = DictDirs();
    auto e = std::make_unique<hdTrans::Engine>(false, hdTrans::CacheWrite::Off);
    if (!e->Open(dirs)) return 0;
    AdminLock lock;
    if (generation != g_generation) return 0;
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine);
    g_dirs = std::move(dirs);
    ++g_generation;
    return 1;
}

int __stdcall J2K_InitializeEx(char* dicPath, char* /*mainDir*/) { return J2K_Initialize(dicPath); }

int __stdcall J2K_Terminate() {
    AdminLock lock;
    ++g_generation;
    g_session.reset();
    g_engine.reset();
    g_dirs.clear();
    return 1;
}

char* __stdcall J2K_TranslateMMNT(HWND /*hWnd*/, char* text) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_engine || !text) return nullptr;
    const std::string utf8 = Utf8FromWide(Widen(kShiftJis, text, static_cast<int>(std::strlen(text))));
    const std::wstring w = WideFromUtf8(TranslateUtf8(utf8));
    return Block(Narrow(kCp949, w.data(), static_cast<int>(w.size())));
}

wchar_t* __stdcall J2K_TranslateMMNTW(HWND /*hWnd*/, const wchar_t* text) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_adminsDone.wait(lock, [] { return g_waitingAdmins == 0; });
    if (!g_engine || !text) return nullptr;
    const std::string utf8 = Narrow(CP_UTF8, text, static_cast<int>(std::wcslen(text)));
    return Block(WideFromUtf8(TranslateUtf8(utf8)));
}

void* __stdcall J2K_TranslateMM(HWND hWnd, char* text) { return J2K_TranslateMMNT(hWnd, text); }
void* __stdcall J2K_TranslateMMEx(HWND hWnd, char* text) { return J2K_TranslateMMNT(hWnd, text); }
void* __stdcall J2K_TranslateChat(char* text) { return J2K_TranslateMMNT(nullptr, text); }
void* __stdcall J2K_TranslateFM(HWND /*hWnd*/, char* /*fileName*/) { return nullptr; }

void __stdcall J2K_FreeMem(void* block) {
    if (block) HeapFree(GetProcessHeap(), 0, block);
}

int __stdcall J2K_StopTranslation(void* /*target*/) { return 1; }
void __stdcall J2K_ReloadUserDict() {
    std::uint64_t generation;
    std::vector<std::string> dirs;
    {
        AdminLock lock;
        if (!g_engine) return;
        generation = g_generation;
        dirs = g_dirs;
    }
    auto e = std::make_unique<hdTrans::Engine>(false, hdTrans::CacheWrite::Off);
    if (!e->Open(dirs)) return;
    AdminLock lock;
    if (generation != g_generation) return;
    g_session.reset();
    g_engine = std::move(e);
    g_session = std::make_unique<hdTrans::Session>(*g_engine);
    ++g_generation;
}
void __stdcall J2K_SetField(int) {}
void __stdcall J2K_SetHnj2han(int) {}
void __stdcall J2K_SetDelJPN(int) {}
void __stdcall J2K_SetJWin(int) {}
void __stdcall J2K_SetPriorDict(char* /*path*/) {}
char* __stdcall J2K_GetPriorDict() { return nullptr; }
int __stdcall J2K_SetProperty(int /*id*/, void* /*value*/, int /*size*/) { return 0; }
int __stdcall J2K_GetProperty(int /*id*/, void* /*value*/, int /*size*/) { return 0; }
void __stdcall J2K_GetJ2KMainDir() {}

}  // extern "C"
