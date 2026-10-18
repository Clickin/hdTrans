#pragma once
// Terminal forms and the punctuation of the text-path passes that read them.
#include <string_view>

#include "hdTrans/word/util.h"

namespace hdTrans::word {

// The attributive endings the terminal pass rewrites: for a verb ('4') or adjective ('7')
// item whose text ends in one of last, the attributive and the terminal form of the same tense.
struct TerminalForm {
    char cls;
    std::string_view attributive;
    std::string_view terminal;
    std::string_view last[9];  // the characters before which the form applies
    int lastCount;
};

inline constexpr TerminalForm kTerminalForms[] = {
    {'4', "0", "4다", {"う", "く", "ぐ", "す", "つ", "ぬ", "ぶ", "む", "る"}, 9},
    {'4', "8", "5다", {"た", "だ"}, 2},
    {'7', "4", "1다", {"い"}, 1},
};

// Punctuation items the passes look past or stop at.
inline constexpr std::string_view kCommaItems[] = {"、", "，", ","};
inline constexpr std::string_view kClosingQuoteItems[] = {"」", "』", "）", "】", "”", "’", "〉", "》", "］", "｝", "〕", ")", "]"};
inline constexpr std::string_view kSentenceEndItems[] = {"？", "！", "?", "!"};

}  // namespace hdTrans::word
