#pragma once
// Rules compiled to C++ by the rule compiler (src/rulec), and the atom functions their code
// is made of. Each atom function evaluates one atom of eval.cpp with the same calls in the
// same order: the left word, then the right word of a word operand, then the test.
#include <cstdint>
#include <span>
#include <string_view>

#include "core/rules/rules.h"

namespace hdTrans::rules::compiled {

using RuleCode = bool (*)(Item*);

// The code of one rule and the hash of the record line it was compiled from.
struct CompiledRule {
    RuleCode code;
    uint64_t hash;
};

// FNV-1a 64 of a record line without its line end.
constexpr uint64_t RecordHash(std::string_view record) {
    uint64_t h = 14695981039346656037ull;
    for (const char c : record) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    return h;
}

// The compiled rules by record index; a null code is a rule left to the interpreter.
std::span<const CompiledRule> LocalRules();
std::span<const CompiledRule> GlobalRules();

inline Item* At(Item* p, int delta) { return delta == 0 || p == nullptr ? p : p->GetObject(delta); }

inline bool SrcEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->CheckSrc(s);
}
inline bool SrcNe(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && !w->CheckSrc(s);
}
inline bool SrcEqW(Item* p, int d, int r) {
    Item* w = At(p, d);
    Item* wi = At(p, r);
    return w != nullptr && w->CheckLLSrc(wi);
}
inline bool DstEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->CheckDst(s, w->GetCurrentCompDI());
}
inline bool DstNe(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && !w->CheckDst(s, w->GetCurrentCompDI());
}
inline bool DstSet(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->AssignDst(w->GetCurrentCompDI(), s);
}
inline bool DstSetW(Item* p, int d, int r) {
    Item* w = At(p, d);
    Item* wi = At(p, r);
    return w != nullptr && w->AssignLLDst(wi, w->GetCurrentCompDI());
}
inline bool PumsaEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->CheckPumsa(s, w->GetCurrentCompDI());
}
inline bool PumsaNe(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && !w->CheckPumsa(s, w->GetCurrentCompDI());
}
inline bool AttrEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->CheckAttr(s, w->GetCurrentCompDI());
}
inline bool AttrNe(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && !w->CheckAttr(s, w->GetCurrentCompDI());
}
inline bool AttrMinusEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->MinAttrOperation(w->GetCurrentCompDI(), s);
}
inline bool AttrMinusEqW(Item* p, int d, int r) {
    Item* w = At(p, d);
    Item* wi = At(p, r);
    return w != nullptr && wi != nullptr && w->CheckLLAttr(kOpMinSubAttr, wi, w->GetCurrentCompDI());
}
inline bool GramEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->CheckGAttr(s, w->GetCurrentCompDI());
}
inline bool GramNe(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && !w->CheckGAttr(s, w->GetCurrentCompDI());
}
inline bool GramMinusEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->MinGAttrOperation(w->GetCurrentCompDI(), s);
}
inline bool GobiSrcEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->CheckGobiSrc(s);
}
inline bool GobiSrcNe(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && !w->CheckGobiSrc(s);
}
inline bool GobiDstEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->CheckGobiDst(s, w->GetCurrentCompUmiDI());
}
inline bool GobiDstSet(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->AssignGobiDst(w->GetCurrentCompUmiDI(), s);
}
// Takes the base candidate index, not the ending one.
inline bool GobiDstSetW(Item* p, int d, int r) {
    Item* w = At(p, d);
    Item* wi = At(p, r);
    return w != nullptr && w->AssignLLGobiDst(wi, w->GetCurrentCompDI());
}
inline bool GobiPumsaEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->CheckGobiPumsa(s, w->GetCurrentCompUmiDI());
}
inline bool GobiAttrEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->CheckGobiAttr(s, w->GetCurrentCompUmiDI());
}
inline bool GobiAttrMinusEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->MinGobiAttrOperation(w->GetCurrentCompUmiDI(), s);
}
inline bool GobiGramEq(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && w->CheckGobiGAttr(s, w->GetCurrentCompUmiDI());
}
inline bool GobiGramNe(Item* p, int d, std::string_view s) {
    Item* w = At(p, d);
    return w != nullptr && !w->CheckGobiGAttr(s, w->GetCurrentCompUmiDI());
}

}  // namespace hdTrans::rules::compiled
