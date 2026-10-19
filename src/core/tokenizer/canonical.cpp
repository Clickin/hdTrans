#include "core/tokenizer/canonical.h"

#include <algorithm>

namespace hdTrans::tokenizer {

namespace {

std::optional<std::string_view> Find(std::span<const internal::CharForm> table, char32_t c) {
    const auto it = std::lower_bound(table.begin(), table.end(), c,
                                     [](const internal::CharForm& f, char32_t ch) { return f.ch < ch; });
    if (it == table.end() || it->ch != c) return std::nullopt;
    return std::string_view(it->form);
}

}  // namespace

std::optional<std::string_view> CanonicalForm(char32_t c) { return Find(internal::CanonicalFormTable(), c); }

std::optional<std::string_view> FirstForm(char32_t c) { return Find(internal::FirstFormTable(), c); }

char32_t WidenHalfWidth(char32_t c) {
    if (c >= 0xFF61 && c <= 0xFF9F) return internal::HalfWidthTable()[c - 0xFF61];
    return c;
}

}  // namespace hdTrans::tokenizer
