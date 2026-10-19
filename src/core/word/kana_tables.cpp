#include "core/word/kana_tables.h"

namespace hdTrans::word {

int KanaMatrixIndex(std::string_view cell) {
    const auto table = internal::KanaMatrixTable();
    for (size_t i = 0; i < table.size(); i++) {
        if (cell == table[i]) return static_cast<int>(i);
    }
    return -1;
}

std::string_view KanaMatrixKatakana(int index) {
    if (index < 0 || index >= kKanaMatrixRows * 5) return {};
    return internal::KanaMatrixTable()[index];
}

std::string_view KanaCell(int row, int n) {
    if (row < 0 || row >= kKanaMatrixRows || n < 0 || n >= kKanaCells) return {};
    return internal::KanaRowTable(row)[n];
}

int FindKanaCell(int row, std::string_view cell) {
    if (row < 0 || row >= kKanaMatrixRows) return -1;
    const auto cells = internal::KanaRowTable(row);
    for (int n = 0; n < kKanaCells; n++) {
        if (cell == cells[n]) return n;
    }
    return -1;
}

int MatchHiragana(std::string_view text) {
    const auto rows = HiraganaRows();
    for (size_t i = 0; i < rows.size(); i++) {
        if (text.starts_with(rows[i].kana)) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace hdTrans::word
