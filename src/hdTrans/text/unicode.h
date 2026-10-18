#pragma once
// Unicode character predicates over generated range tables (unicode_gen.cpp), so results
// do not depend on the C library's locale or Unicode version.
namespace hdTrans::text {

bool IsKanji(char32_t r);             // script Han: kanji and hanja, including 々
bool IsClosePunctuation(char32_t r);  // category Pe
bool IsOtherSymbol(char32_t r);       // category So
bool IsLetter(char32_t r);            // category L
bool IsPunct(char32_t r);             // category P
bool IsSymbol(char32_t r);            // category S
bool IsDigit(char32_t r);             // category Nd
// White space: \t \n \v \f \r, space, U+0085, U+00A0 and the White_Space property.
bool IsSpace(char32_t r);

}  // namespace hdTrans::text
