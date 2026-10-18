#pragma once
// Readings of single CJK characters.
#include <optional>
#include <span>

namespace hdTrans::dict {

// The Hangul syllable whose final decides the particle after the hanja c; none for
// other characters.
std::optional<char32_t> Reading(char32_t c);

// The character written for the kanji c (and for 〇, ・ and 々) when kanji are
// written as Hangul: its reading syllable, or the symbol it stands for; none for
// other characters.
std::optional<char32_t> Written(char32_t c);

namespace internal {

struct CharReading {
    char32_t ch;
    char32_t reading;  // 0: none
    char32_t written;  // 0: none
};

// All rows, sorted by ch, each ch once.
std::span<const CharReading> CharReadingTable();

}  // namespace internal

}  // namespace hdTrans::dict
