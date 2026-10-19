#include "core/dict/folders.h"

#include <filesystem>
#include <system_error>

namespace hdTrans::dict {

namespace {

std::string Join(std::string_view dir, std::string_view name) {
    const std::filesystem::path path =
        std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(dir.data()), dir.size())) /
        std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()));
    const std::u8string s = path.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

}  // namespace

std::string ShippedFolder(std::string_view programDir) { return Join(programDir, "dicts"); }

std::string DefaultFolder(std::string_view programDir) {
    if (programDir.empty()) return "dicts";
    const std::string shipped = ShippedFolder(programDir);
    std::error_code ec;
    const std::u8string s(shipped.begin(), shipped.end());
    return std::filesystem::is_directory(std::filesystem::path(s), ec) ? shipped : "dicts";
}

std::string UserFile(std::string_view programDir) {
    return programDir.empty() ? std::string() : Join(programDir, "userdict.tsv");
}

}  // namespace hdTrans::dict
