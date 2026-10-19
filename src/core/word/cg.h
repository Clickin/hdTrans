#pragma once
// CG-lite: remove-only rules over the candidates of the misread decisions (misread.h).
//
// A rule table (src/core/dict/builtin/extra/cg_*.tsv) has rows "tier action target condition
// note". action is REMOVE or SELECT; target and condition are literals separated by spaces: a
// feature of the candidate, "feature*" for a prefix, "name={a,b}" for either value, "!literal"
// for its negation, "chosen" for the candidate the engine chose; "\s" is a space. Target
// literals are tested on each live candidate, condition literals on the chosen one; the
// condition literal "kept" holds while the chosen candidate is live, "any:L" / "none:L" when
// some / no other live candidate matches L. Tiers run in ascending order, each to a fixpoint;
// REMOVE never removes every live candidate and SELECT applies only when some but not all live
// candidates match. When the chosen candidate is removed, the first live one replaces it.
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/word/misread.h"

namespace hdTrans::word {

struct Flusher;

struct CgLiteral {
    struct Alternative {
        bool prefix = false;
        std::string text;
    };
    bool negated = false;
    std::vector<Alternative> alternatives;
};

struct CgCondition {
    enum class Kind : uint8_t { Chosen, Kept, Any, None };
    Kind kind = Kind::Chosen;
    CgLiteral literal;
};

struct CgRule {
    int tier = 0;
    bool select = false;
    std::vector<CgLiteral> target;
    std::vector<CgCondition> condition;
    std::string note;
};

// The rules of a table sorted by tier (stable); rows with an unknown action are skipped.
std::vector<CgRule> ParseCgRules(std::string_view text);

// The built-in rules: the hand rules (cg_rules.tsv), the learned rules (cg_learned.tsv), or
// both in that order.
std::span<const CgRule> BuiltinCgRules(bool hand, bool learned);

// The index of the candidate of decision that replaces the chosen one, or -1: the chosen
// candidate's provenance may be replaced and the rules remove it.
int CgReplacement(const MisreadDecision& decision, std::span<const CgRule> rules);

// The pass (Flush, cgRules / cgLearned): every replacement of the chain's decisions, decided
// before any is made.
void FilterCg(Flusher& f);

}  // namespace hdTrans::word
