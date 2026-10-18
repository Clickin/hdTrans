#pragma once
// Canonical (Korean-side) forms of single characters.
#include <optional>
#include <span>
#include <string_view>

namespace hdTrans::tokenizer {

// The canonical form of c when it differs from c: the KS X 1001 form of a kanji, its
// Hangul reading or □ when KS X 1001 has no such hanja, the full-width form of a
// half-width character, the Korean usage of punctuation (、 -> ", ", 。 -> ". ").
// Forms may begin or end with spaces.
std::optional<std::string_view> CanonicalForm(char32_t c);

// The canonical form of a whole text that starts with c (？ -> "?", 〒 -> "(우)");
// the rest of such a text is dropped.
std::optional<std::string_view> FirstForm(char32_t c);

// The full-width form of a half-width katakana or punctuation character
// U+FF61..U+FF9F (ｱ -> ア, ｡ -> 。, ﾞ -> ゛); c itself for other characters.
char32_t WidenHalfWidth(char32_t c);

namespace internal {

struct CharForm {
    char32_t ch;
    const char* form;
};

// Sorted by ch, each ch once.
std::span<const CharForm> CanonicalFormTable();
std::span<const CharForm> FirstFormTable();
// The full-width forms of U+FF61..U+FF9F.
std::span<const char32_t, 63> HalfWidthTable();

}  // namespace internal

}  // namespace hdTrans::tokenizer
