#include "core/dict/char_readings.h"

#include <algorithm>

namespace hdTrans::dict {

namespace {

const internal::CharReading* Row(char32_t c) {
    const auto table = internal::CharReadingTable();
    const auto it = std::lower_bound(table.begin(), table.end(), c,
                                     [](const internal::CharReading& r, char32_t ch) { return r.ch < ch; });
    return it != table.end() && it->ch == c ? &*it : nullptr;
}

}  // namespace

std::optional<char32_t> Reading(char32_t c) {
    const auto* r = Row(c);
    if (!r || r->reading == 0) return std::nullopt;
    return r->reading;
}

std::optional<char32_t> Written(char32_t c) {
    const auto* r = Row(c);
    if (!r || r->written == 0) return std::nullopt;
    return r->written;
}

}  // namespace hdTrans::dict
