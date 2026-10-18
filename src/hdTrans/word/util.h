#pragma once
// Text helpers of the word stage. Some texts are measured in width units: 1 for ASCII
// and half-width katakana, 2 for every other character (an invalid byte included).
#include <cstddef>
#include <string>
#include <string_view>

#include "hdTrans/text/utf8.h"

namespace hdTrans::word {

int Width(char32_t r);
int WidthOf(std::string_view s);

// The byte offset of width unit n of s (s.size() past the end) and whether a
// character boundary lies there; inside a character, the offset after it.
struct UnitPos {
    std::size_t offset;
    bool boundary;
};
UnitPos UnitOffset(std::string_view s, int n);

// A text cut out of another by width. A cut whose ends fall inside a character is
// broken: it reads as its text followed by the byte 0xff, so it equals no text and is
// never empty.
struct Window {
    std::string_view text;
    bool broken = false;

    bool operator==(std::string_view s) const { return !broken && text == s; }
    bool Empty() const { return !broken && text.empty(); }
    bool StartsWith(std::string_view p) const { return text.starts_with(p); }
    // The text as one string, with the 0xff byte when broken.
    std::string Str() const;
};

// The text of s from width unit first, count units long, clamped to s.
Window Cut(std::string_view s, int first, int count);
// The first and the last n width units of s.
Window Left(std::string_view s, int n);
Window Right(std::string_view s, int n);

// s from byte offset i, clamped.
inline std::string_view From(std::string_view s, long i) {
    if (i < 0) i = 0;
    if (static_cast<std::size_t>(i) > s.size()) i = static_cast<long>(s.size());
    return s.substr(static_cast<std::size_t>(i));
}

// s[i], or 0 outside s.
inline char At(std::string_view s, long i) {
    return i < 0 || static_cast<std::size_t>(i) >= s.size() ? '\0' : s[static_cast<std::size_t>(i)];
}

inline bool Has(std::string_view s, std::string_view sub) { return s.find(sub) != std::string_view::npos; }

// The first and the last character of s; "" for an empty string. An invalid byte is a
// character of its own.
std::string_view FirstChar(std::string_view s);
std::string_view LastChar(std::string_view s);

// The rune that ends s: U+FFFD of size 1 for an invalid sequence, size 0 for "".
text::Rune DecodeLastRune(std::string_view s);

// The tokenizer class of the first character (13 hiragana, 14 katakana, 24 kanji and
// the like), 0 for "".
int Classify(std::string_view text);
int Classify(const Window& w);

inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }
inline bool IsAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
inline bool IsPunct(char c) { return c > ' ' && c < 0x7F && !IsDigit(c) && !IsAlpha(c); }

// HasPrefix for an ASCII prefix, ignoring ASCII case.
bool HasPrefixFold(std::string_view s, std::string_view prefix);

// The index of お, ご or 御 when c is one of them, -1 otherwise.
int FindHonorificPrefix(std::string_view c);

// Character classes for the kana boundaries of keys.
enum NameClass : int { kClassOther, kClassHiragana, kClassKatakana, kClassKanji };
// The class of the first character of c: ぁ-ん, ァ-ヶ with ー ヽ ヾ, kanji with 々 〆
// (not 〇).
int NameClassOf(std::string_view c);

// Whether s has a byte of set.
inline bool AnyOf(std::string_view set, char c) { return set.find(c) != std::string_view::npos; }

// Whether c, a character, occurs in s (as a substring, so "" never does).
inline bool ContainsChar(std::string_view s, std::string_view c) { return !c.empty() && Has(s, c); }

}  // namespace hdTrans::word
