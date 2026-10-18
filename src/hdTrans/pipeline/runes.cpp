#include "hdTrans/pipeline/runes.h"

namespace hdTrans::pipeline::internal {

text::Rune LastRune(std::string_view s) {
    if (s.empty()) return {text::kReplacement, 0};
    const auto last = static_cast<unsigned char>(s.back());
    if (last < 0x80) return {last, 1};
    // The character starts at the nearest byte before the last one that is not a
    // continuation byte, at most three back.
    const size_t end = s.size();
    const size_t lim = end >= 4 ? end - 4 : 0;
    for (size_t j = end - 1; j-- > lim;) {
        if ((static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) continue;
        const text::Rune r = text::DecodeRune(s.substr(j));
        if (j + r.size != end) break;
        return r;
    }
    return {text::kReplacement, 1};
}

bool ValidUtf8(std::string_view s) {
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.cp == text::kReplacement && r.size == 1) return false;
        s.remove_prefix(r.size);
    }
    return true;
}

bool RuneIn(char32_t c, std::u32string_view set) { return set.find(c) != std::u32string_view::npos; }

}  // namespace hdTrans::pipeline::internal
