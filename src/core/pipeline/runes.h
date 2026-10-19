#pragma once
// Character classes and UTF-8 scanning shared by the protect and segment stages.
#include <cstddef>
#include <string_view>

#include "core/text/utf8.h"

namespace hdTrans::pipeline::internal {

// The last character of s and its size; U+FFFD and 1 when s ends in an invalid
// sequence, U+FFFD and 0 for an empty string.
text::Rune LastRune(std::string_view s);

// Whether s holds no invalid UTF-8 sequence.
bool ValidUtf8(std::string_view s);

// Whether c is one of the characters of set.
bool RuneIn(char32_t c, std::u32string_view set);

// ぁ..ゖ
inline bool IsHiraganaRune(char32_t r) { return r >= 0x3041 && r <= 0x3096; }
// ァ..ヶ and ー
inline bool IsKatakanaRune(char32_t r) { return (r >= 0x30A1 && r <= 0x30F6) || r == 0x30FC; }
// The characters that mask a letter of a word (伏せ字): ○ ● ◯ 〇 ◎ × ✕ ＊ * ■ □ △ ▲ ◇ ◆.
inline bool IsMaskRune(char32_t r) {
    return std::u32string_view(U"○●◯〇◎×✕＊*■□△▲◇◆").find(r) != std::u32string_view::npos;
}
// CJK ideographs and extension A, and 々.
inline bool IsKanjiRune(char32_t r) { return (r >= 0x4E00 && r <= 0x9FFF) || (r >= 0x3400 && r <= 0x4DBF) || r == 0x3005; }
// Kana (U+3041..U+30FF), CJK ideographs with extension A and the compatibility
// ideographs, 々 and 〆.
inline bool IsJapaneseRune(char32_t r) {
    return (r >= 0x3041 && r <= 0x30FF) || (r >= 0x3400 && r <= 0x4DBF) || (r >= 0x4E00 && r <= 0x9FFF) ||
           (r >= 0xF900 && r <= 0xFAFF) || r == 0x3005 || r == 0x3006;
}

// The length in bytes of the leading run of s whose characters satisfy in.
template <class Pred>
size_t RunOf(std::string_view s, Pred in) {
    size_t n = 0;
    while (n < s.size()) {
        const text::Rune r = text::DecodeRune(s.substr(n));
        if (!in(r.cp)) break;
        n += r.size;
    }
    return n;
}

}  // namespace hdTrans::pipeline::internal
