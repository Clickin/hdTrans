#pragma once
// Fixture sizes come from MANIFEST; record totals are counted independently of replay.
#include <charconv>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "hdtest.h"

namespace hdtest {

inline std::map<std::string, size_t> RecordCounts(std::string_view text) {
    std::map<std::string, size_t> counts;
    while (!text.empty()) {
        const auto lf = text.find('\n');
        const auto line = text.substr(0, lf);
        if (!line.empty() && line[0] != '#') counts[std::string(line.substr(0, line.find('\t')))]++;
        text.remove_prefix(lf == std::string_view::npos ? text.size() : lf + 1);
    }
    return counts;
}

inline size_t CountRecords(std::string_view text, std::string_view tag) {
    size_t count = 0;
    while (!text.empty()) {
        const auto lf = text.find('\n');
        const auto line = text.substr(0, lf);
        if (line.substr(0, line.find('\t')) == tag) ++count;
        text.remove_prefix(lf == std::string_view::npos ? text.size() : lf + 1);
    }
    return count;
}

inline size_t LineCount(std::string_view text) {
    size_t count = 0;
    while (!text.empty()) {
        const auto lf = text.find('\n');
        ++count;
        text.remove_prefix(lf == std::string_view::npos ? text.size() : lf + 1);
    }
    return count;
}

inline std::vector<std::string> ManifestSets(const std::string& directory, std::string_view suffix) {
    std::ifstream manifest(std::filesystem::path(directory) / "MANIFEST", std::ios::binary);
    if (!manifest.is_open()) {
        Fail(__FILE__, __LINE__, "cannot read fixture MANIFEST in " + directory);
        return {};
    }
    std::map<std::string, uintmax_t> files;
    bool valid = true;
    std::string line;
    while (std::getline(manifest, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("commit ") || line.starts_with("dicts ")) continue;
        const auto space = line.find(' ');
        uintmax_t size = 0;
        const auto end = line.data() + (space == std::string::npos ? line.size() : space);
        const auto parsed = std::from_chars(line.data(), end, size);
        if (space == std::string::npos || parsed.ec != std::errc{} || parsed.ptr != end || space + 1 == line.size()) {
            Fail(__FILE__, __LINE__, "invalid fixture MANIFEST row: " + line);
            valid = false;
            continue;
        }
        const auto name = line.substr(space + 1);
        std::error_code ec;
        const auto path = std::filesystem::path(directory) / name;
        if (!files.emplace(name, size).second || std::filesystem::file_size(path, ec) != size || ec) {
            Fail(__FILE__, __LINE__, "missing, duplicate or truncated fixture: " + path.string());
            valid = false;
        }
    }
    if (!manifest.eof() || files.empty()) {
        Fail(__FILE__, __LINE__, "empty or unreadable fixture MANIFEST in " + directory);
        valid = false;
    }
    std::vector<std::string> sets;
    if (!valid) return sets;
    for (const auto& [name, size] : files) {
        if (name.ends_with(suffix)) sets.push_back(name.substr(0, name.size() - suffix.size()));
    }
    if (sets.empty()) Fail(__FILE__, __LINE__, "no fixture files ending in " + std::string(suffix));
    return sets;
}

inline bool CheckManifest(const std::string& directory) {
    return !ManifestSets(directory, ".tsv").empty();
}

}  // namespace hdtest
