#pragma once

#include <optional>
#include <string>

namespace hdTrans::internal {

// Call under the front end's admin lock. Removing the setting restores the prior environment.
struct ConfigCache {
#ifdef _WIN32
    std::optional<std::wstring> previous;
#else
    std::optional<std::string> previous;
#endif
    bool active = false;
    bool Apply(const std::optional<std::string>& directory);
};

}  // namespace hdTrans::internal
