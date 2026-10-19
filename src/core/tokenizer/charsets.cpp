#include "core/tokenizer/charsets.h"

#include <algorithm>

namespace hdTrans::tokenizer {

bool IsNonKanjiHan(char32_t c) {
    const auto t = internal::NonKanjiHanTable();
    return std::binary_search(t.begin(), t.end(), c);
}

bool IsCp932Symbol(char32_t c) {
    const auto t = internal::Cp932SymbolTable();
    return std::binary_search(t.begin(), t.end(), c);
}

bool IsSjisHighLead(char32_t c) {
    const auto t = internal::SjisHighLeadTable();
    return std::binary_search(t.begin(), t.end(), c);
}

}  // namespace hdTrans::tokenizer
