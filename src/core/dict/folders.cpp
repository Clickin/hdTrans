#include "core/dict/folders.h"

#include <filesystem>
#include <system_error>

namespace hdTrans::dict {
namespace {

std::filesystem::path PathOf(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string Lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

}  // namespace

std::string FindChild(std::string_view folder, std::string_view name, bool directory) {
    const auto parent = PathOf(folder);
    const auto wanted = Lower(std::string(name));
    std::string matched;
    std::error_code error;
    std::filesystem::directory_iterator entries(folder.empty() ? std::filesystem::path(".") : parent, error), end;
    while (!error && entries != end) {
        const auto child = entries->path();
        const auto filename = Utf8(child.filename());
        std::error_code kindError;
        const bool kind = directory ? entries->is_directory(kindError) : entries->is_regular_file(kindError);
        if (!kindError && kind) {
            if (filename == name) return Utf8(child);
            const auto candidate = Utf8(child);
            if (Lower(filename) == wanted && (matched.empty() || candidate < matched)) matched = candidate;
        }
        entries.increment(error);
    }
    return matched.empty() ? Utf8(parent / PathOf(name)) : matched;
}

std::string DataFolder(std::string_view programDir) {
    if (programDir.empty()) return {};
    const auto folder = FindChild(programDir, "hdTrans", true);
    std::error_code error;
    return std::filesystem::is_directory(PathOf(folder), error) ? folder : std::string(programDir);
}

std::string UserFolder(std::string_view programDir) {
    return FindChild(DataFolder(programDir), "user", true);
}

std::string ShippedFolder(std::string_view programDir) { return FindChild(DataFolder(programDir), "dicts", true); }

std::string DefaultFolder(std::string_view programDir) {
    if (programDir.empty()) return "dicts";
    const auto folder = ShippedFolder(programDir);
    std::error_code error;
    return std::filesystem::is_directory(PathOf(folder), error) ? folder : "dicts";
}

namespace {

std::string UserPath(std::string_view programDir, std::string_view name) {
    const auto path = FindChild(UserFolder(programDir), name);
    std::error_code error;
    if (std::filesystem::is_regular_file(PathOf(path), error)) return path;
    if (DataFolder(programDir) == programDir) {
        const auto legacy = FindChild(programDir, name);
        if (std::filesystem::is_regular_file(PathOf(legacy), error)) return legacy;
    }
    return path;
}

}  // namespace

std::string UserFile(std::string_view programDir) {
    return programDir.empty() ? std::string() : UserPath(programDir, "userdict.tsv");
}

std::string ConfigFile(std::string_view programDir) { return UserPath(programDir, "hdtrans.config.yaml"); }

std::string EhndFolder(std::string_view programDir) {
    if (programDir.empty()) return {};
    const auto folder = FindChild(programDir, "Ehnd", true);
    std::error_code error;
    return std::filesystem::is_directory(PathOf(folder), error) ? folder : std::string();
}

}  // namespace hdTrans::dict
