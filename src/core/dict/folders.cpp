#include "core/dict/folders.h"

#include <filesystem>
#include <system_error>

namespace hdTrans::dict {

namespace {

std::filesystem::path PathOf(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Join(std::string_view dir, std::string_view name) {
    const std::u8string s = (PathOf(dir) / PathOf(name)).u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

}  // namespace

std::string ShippedFolder(std::string_view programDir) { return Join(programDir, "dicts"); }

std::string DefaultFolder(std::string_view programDir) {
    if (programDir.empty()) return "dicts";
    const std::string shipped = ShippedFolder(programDir);
    std::error_code ec;
    return std::filesystem::is_directory(PathOf(shipped), ec) ? shipped : "dicts";
}

std::string UserFile(std::string_view programDir) {
    return programDir.empty() ? std::string() : Join(programDir, "userdict.tsv");
}

}  // namespace hdTrans::dict
