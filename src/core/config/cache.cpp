#include "core/config/cache.h"

#include <cstdlib>
#ifdef _WIN32
#include <filesystem>
#endif

namespace hdTrans::internal {

bool ConfigCache::Apply(const std::optional<std::string>& directory) {
    if (!directory && !active) return true;
    if (!active) {
        previous.reset();
#ifdef _WIN32
        if (const wchar_t* value = _wgetenv(L"HDTRANS_CACHE_DIR")) previous = value;
#else
        if (const char* value = std::getenv("HDTRANS_CACHE_DIR")) previous = value;
#endif
    }
#ifdef _WIN32
    const std::wstring value = directory
        ? std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(directory->data()), directory->size())).native()
        : previous.value_or(L"");
    const int error = _wputenv_s(L"HDTRANS_CACHE_DIR", value.c_str());
#else
    const int error = directory ? setenv("HDTRANS_CACHE_DIR", directory->c_str(), 1)
                               : previous ? setenv("HDTRANS_CACHE_DIR", previous->c_str(), 1) : unsetenv("HDTRANS_CACHE_DIR");
#endif
    if (error != 0) return false;
    active = directory.has_value();
    if (!active) previous.reset();
    return true;
}

}  // namespace hdTrans::internal
