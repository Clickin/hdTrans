#pragma once
// The rules program of the word stage, loaded from the embedded table (rules_gen.cpp).
#include <span>
#include <string_view>

#include "core/rules/rules.h"

namespace hdTrans::internal {

// The lines of the rules program, each ending in "\n" (rules_gen.cpp).
std::span<const std::string_view> RuleLines();

// The loaded program; loaded on first use and immutable afterwards.
const rules::Program& RulesProgram();
// Whether RulesProgram loaded.
bool RulesLoaded();

}  // namespace hdTrans::internal
