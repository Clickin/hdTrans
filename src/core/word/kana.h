#pragma once
// Kana written in Hangul for words the dictionary does not know.
#include <string>
#include <string_view>

namespace hdTrans::word {

// ァ-ヺ and ー.
inline bool IsKatakana(char32_t r) { return (r >= U'ァ' && r <= U'ヺ') || r == U'ー'; }

// Writes every katakana run of s in Hangul into out and drops the katakana no rule
// transliterates; an invalid byte is written as U+FFFD. False (a fault, out
// unspecified) when a context rule does not find the text it replaces: the engine
// then drops the rest of the chunk.
bool TransliterateKatakana(std::string_view s, std::string& out);

// TransliterateKatakana, but ツ as 츠 (グランツ 그란츠, ツバサ 츠바사) where the
// engine writes 트 for loanwords (ツリー 트리); a ッ before ツ goes (ミッツ 미츠).
bool TransliterateName(std::string_view s, std::string& out);

// A hiragana text in Hangul by the hiragana rows; full-width digits and letters (but
// ｖ) become ASCII, and ん gives the syllable before it a final: ㅇ before ㄱ ㅋ ㅎ,
// ㅁ before ㅁ ㅂ, ㄴ otherwise (by the initial of the last syllable of the next
// row's Hangul). Characters no row starts with are dropped.
std::string TransliterateHiragana(std::string_view s);

}  // namespace hdTrans::word
