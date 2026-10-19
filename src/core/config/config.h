#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hdTrans::internal {

struct Config {
    std::optional<std::vector<std::string>> dicts;
    std::optional<std::string> userdict;
    std::optional<std::string> ehnd;
    std::optional<std::string> cache;
    bool cg = true;
    bool misread = false;
    bool reference = false;
};

struct ConfigDiagnostic {
    std::size_t line = 0;  // 1-based; 0 for file errors.
    std::string message;
};

struct ConfigResult {
    Config settings;
    std::vector<ConfigDiagnostic> diagnostics;
    bool found = false;
};

std::string ConfigPath(std::string_view folder);
// Missing files are optional. Invalid entries are skipped; earlier valid entries survive syntax errors.
ConfigResult LoadConfig(std::string_view path);
std::string ConfigErrors(const ConfigResult& result, std::string_view path);

}  // namespace hdTrans::internal
