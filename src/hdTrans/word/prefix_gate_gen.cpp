// Generated table; do not edit.
#include "hdTrans/word/conjugation.h"

namespace hdTrans::word::internal {

namespace {

constexpr const char* kGate[kPrefixGates] = {
    "い", "かった", "く", "ければ", "き", "ず", "にな", "まい", "ますまい", "まるんです",
    "まるんですか", "きま", "しま", "死に", "に", "な", "ま", "始", "なさい", "ながら",
    "つつあ", "はじ", "がた", "難", "やす", "安", "よう", "うる", "たかった", "たい",
    "たく", "たければ", "たき", "、",
};

}  // namespace

std::span<const char* const, kPrefixGates> PrefixGateTable() { return kGate; }

}  // namespace hdTrans::word::internal
