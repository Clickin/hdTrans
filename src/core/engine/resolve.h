#pragma once
// The resolve stage of the word pipeline, one sentence at a time.
#include <cstddef>

#include "core/output/families.h"
#include "core/rules/rules.h"
#include "core/word/analyze.h"

namespace hdTrans::internal {

// Resolves sentence sentence of analysis: its chain is replaced by the resolved chain.
void ResolveSentence(const rules::Program& program, output::Families& families, word::Analysis& analysis,
                     std::size_t sentence);

}  // namespace hdTrans::internal
