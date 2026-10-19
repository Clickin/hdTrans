#include "frontend/module_dir.h"

#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif

namespace hdTrans::frontend {
#if !defined(__EMSCRIPTEN__)
namespace {
const char kModuleAnchor = 0;
}
#endif

std::string ModuleDirectory() {
#if defined(__EMSCRIPTEN__)
    return "/";
#else
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&kModuleAnchor), &module)) return {};
    std::wstring path(256, L'\0');
    for (;;) {
        const DWORD count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (count == 0) return {};
        if (count < path.size()) {
            path.resize(count);
            break;
        }
        path.resize(path.size() * 2);
    }
    const std::filesystem::path image(path);
#else
    Dl_info info{};
    if (!dladdr(&kModuleAnchor, &info) || !info.dli_fname) return {};
    std::error_code error;
    const std::filesystem::path image = std::filesystem::canonical(info.dli_fname, error);
    if (error) return {};
#endif
    const std::u8string dir = image.parent_path().u8string();
    return std::string(reinterpret_cast<const char*>(dir.data()), dir.size());
#endif
}

}  // namespace hdTrans::frontend
