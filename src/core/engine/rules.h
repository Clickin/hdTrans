#pragma once
// The rules program of the word stage, compiled in from src/core/rules/rules.txt.
#include <span>
#include <string_view>

#include "core/rules/rules.h"

namespace hdTrans::internal {

// The lines of src/core/rules/rules.txt, each ending in "\n" (generated at build time).
std::span<const std::string_view> RuleLines();

// The loaded program, with the compiled code (compiled_gen.cpp) of the rules whose record
// lines it was generated from; loaded on first use and immutable afterwards.
const rules::Program& RulesProgram();
// Whether RulesProgram loaded.
bool RulesLoaded();

}  // namespace hdTrans::internal
