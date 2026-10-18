#include "hdTrans/engine.h"

#include "hdTrans/engine/raw.h"

namespace hdTrans {

bool Engine::Open(const std::vector<std::string>& dirs) {
    dirs_ = dirs;
    loaded_ = false;
    return loaded_;
}

bool Engine::Reload() { return Open(dirs_); }

std::string Engine::Translate(std::string_view text) const {
    if (!loaded_) return {};
    return std::string(text);
}

namespace internal {

std::string TranslateRaw(const Engine& e, std::string_view line) {
    if (!e.loaded()) return {};
    return std::string(line);
}

}  // namespace internal
}  // namespace hdTrans
