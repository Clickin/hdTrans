#pragma once
// Internal stages, callable from the CLI and tests; not part of the public API.
#include <string>
#include <string_view>

#include "hdTrans/engine.h"

namespace hdTrans::internal {

// Translates one line through the word pipeline only, without the text path.
std::string TranslateRaw(const Engine& e, std::string_view line);

}  // namespace hdTrans::internal
