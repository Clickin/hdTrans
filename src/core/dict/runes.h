#pragma once
// UTF-8 decoding with the common cases inline, for the loops of loading and lookups.
// The results are those of text::DecodeRune: an invalid byte is U+FFFD and advances
// one byte.
#include <cstddef>
#include <string_view>

#include "core/text/utf8.h"

namespace hdTrans::dict::internal {

inline text::Rune DecodeRuneAt(std::string_view s) {
    if (!s.empty()) {
        const auto b0 = static_cast<unsigned char>(s[0]);
        if (b0 < 0x80) return {b0, 1};
        // Three-byte sequences of U+1000..U+FFFF but the surrogates: lead E1..EF, and
        // after ED a second byte below A0.
        if (b0 >= 0xE1 && b0 <= 0xEF && s.size() >= 3) {
            const auto b1 = static_cast<unsigned char>(s[1]);
            const auto b2 = static_cast<unsigned char>(s[2]);
            if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b0 != 0xED || b1 < 0xA0)) {
                return {static_cast<char32_t>(((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu)), 3};
            }
        }
    }
    return text::DecodeRune(s);
}

inline std::size_t RuneCountOf(std::string_view s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); n++) i += DecodeRuneAt(s.substr(i)).size;
    return n;
}

inline bool ValidUtf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto r = DecodeRuneAt(s.substr(i));
        if (r.size == 1 && r.cp == text::kReplacement) return false;
        i += r.size;
    }
    return true;
}

}  // namespace hdTrans::dict::internal
