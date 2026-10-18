#pragma once
// Character classes and scanning helpers of the name registry. Runs are measured in
// code points; offsets are bytes.
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/text/utf8.h"

namespace hdTrans::names::internal {

inline bool IsKanji(char32_t c) {
    return (c >= 0x4E00 && c <= 0x9FA5) || c == 0x3005 || c == 0x3006 || c == 0x30F6;
}
inline bool IsKatakana(char32_t c) { return (c >= 0x30A1 && c <= 0x30FA) || c == 0x30FC; }
inline bool IsHiragana(char32_t c) { return c >= 0x3041 && c <= 0x3096; }
// Kanji, katakana, hiragana and the ノ that joins kanji in a name.
inline bool IsNameRune(char32_t c) { return IsKanji(c) || IsKatakana(c) || IsHiragana(c) || c == 0x30CE; }
// ASCII white space of the patterns (\s).
inline bool IsPatternSpace(char32_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; }

// Unicode White_Space, the white space trimmed around names and readings.
inline bool IsSpace(char32_t c) {
    return c == ' ' || (c >= '\t' && c <= '\r') || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

// Decodes s from pos: the code point and its size.
inline text::Rune RuneAt(std::string_view s, std::size_t pos) { return text::DecodeRune(s.substr(pos)); }

// Whether every code point of s satisfies pred; false for an empty string.
template <class Pred>
bool AllRunes(std::string_view s, Pred pred) {
    if (s.empty()) return false;
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = RuneAt(s, i);
        if (!pred(r.cp)) return false;
        i += r.size;
    }
    return true;
}

// Whether c is one of the code points of set (UTF-8).
inline bool InRunes(char32_t c, std::string_view set) {
    for (std::size_t i = 0; i < set.size();) {
        const text::Rune r = RuneAt(set, i);
        if (r.cp == c) return true;
        i += r.size;
    }
    return false;
}

inline std::size_t RuneCount(std::string_view s) { return text::RuneCount(s); }

// The first code point of s and its size; {0, 0} for an empty string.
inline text::Rune FirstRune(std::string_view s) {
    if (s.empty()) return {0, 0};
    return text::DecodeRune(s);
}

// The last code point of s that starts before byte end; {0, 0} for none.
inline text::Rune RuneBefore(std::string_view s, std::size_t end) {
    if (end == 0) return {0, 0};
    std::size_t i = end - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) i--;
    return text::DecodeRune(s.substr(i));
}

inline bool IsKanaText(std::string_view s) {
    return AllRunes(s, [](char32_t c) { return IsHiragana(c) || IsKatakana(c); });
}

// A Hangul text: syllables, spaces and middle dots.
inline bool IsHangulText(std::string_view s) {
    return AllRunes(s, [](char32_t c) { return (c >= 0xAC00 && c <= 0xD7A3) || c == ' ' || c == 0xB7; });
}

// Trims White_Space from both ends of s.
inline std::string_view TrimSpace(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e) {
        const text::Rune r = RuneAt(s, b);
        if (!IsSpace(r.cp)) break;
        b += r.size;
    }
    while (e > b) {
        const text::Rune r = RuneBefore(s, e);
        if (!IsSpace(r.cp)) break;
        e -= r.size;
    }
    return s.substr(b, e - b);
}

// Trims space, tab, CR and the ideographic space from both ends (whole-line rows).
inline std::string_view TrimLineSpace(std::string_view s) {
    static constexpr std::string_view kIdeographic = "\xE3\x80\x80";
    auto trimmable = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!s.empty()) {
        if (trimmable(s.front())) {
            s.remove_prefix(1);
        } else if (s.starts_with(kIdeographic)) {
            s.remove_prefix(kIdeographic.size());
        } else {
            break;
        }
    }
    while (!s.empty()) {
        if (trimmable(s.back())) {
            s.remove_suffix(1);
        } else if (s.size() >= kIdeographic.size() && s.ends_with(kIdeographic)) {
            s.remove_suffix(kIdeographic.size());
        } else {
            break;
        }
    }
    return s;
}

// Removes the CR characters at the end of s.
inline std::string_view TrimCR(std::string_view s) {
    while (!s.empty() && s.back() == '\r') s.remove_suffix(1);
    return s;
}

// A run of kanji (with ノ between kanji) or of katakana, as byte offsets into the line.
struct Run {
    std::size_t start = 0, end = 0;
    std::string_view text;
    bool kanji = false;
};

std::vector<Run> RunsOf(std::string_view s);

}  // namespace hdTrans::names::internal
