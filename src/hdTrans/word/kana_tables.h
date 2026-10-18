#pragma once
// Tables of the katakana and hiragana transliteration.
#include <span>
#include <string_view>

namespace hdTrans::word {

struct KanaPair {
    const char* kana;
    const char* hangul;
};

constexpr int kKanaMatrixRows = 15;
constexpr int kKanaCells = 45;

// Loanword spellings (ヴァ -> 바, ショップ -> 숍). First match wins: rows are tried in order.
std::span<const KanaPair> KanaDirect();

// The position of the single katakana cell in the matrix, -1 when it is not there:
// row = index / 5 (ア カ ガ サ ザ タ ダ ナ ハ バ パ マ ラ ヤ ワ), column = index % 5
// (the vowels a i u e o). The empty slots of the ヤ and ワ rows hold "@$".
int KanaMatrixIndex(std::string_view cell);
std::string_view KanaMatrixKatakana(int index);  // "" outside 0..74

// Hangul cell n of matrix row `row`: 0-4 the plain syllables by column, 5-9 variants
// read by position (5, 7, 9: with ャ, ュ, ョ), 10-44 the syllables with the finals
// ㄱ ㄴ ㄹ ㅁ ㅂ ㅅ ㅇ, five cells (columns) each. An empty cell is two spaces;
// "" outside the matrix.
std::string_view KanaCell(int row, int n);
// The first n with KanaCell(row, n) == cell, -1 if none.
int FindKanaCell(int row, std::string_view cell);

// Hiragana prefixes and their Hangul. First match wins: rows are tried in order.
std::span<const KanaPair> HiraganaRows();
// The index of the first row whose hiragana starts text, -1 if none.
int MatchHiragana(std::string_view text);

namespace internal {

std::span<const char* const, kKanaMatrixRows * 5> KanaMatrixTable();
std::span<const char* const, kKanaCells> KanaRowTable(int row);  // row 0..14

}  // namespace internal

}  // namespace hdTrans::word
