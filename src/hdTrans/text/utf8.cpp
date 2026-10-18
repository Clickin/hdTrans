#include "hdTrans/text/utf8.h"

namespace hdTrans::text {

Rune DecodeRune(std::string_view s) {
    if (s.empty()) return {kReplacement, 0};
    const auto b0 = static_cast<unsigned char>(s[0]);
    if (b0 < 0x80) return {b0, 1};

    size_t need;
    char32_t cp;
    char32_t min;
    if ((b0 & 0xE0) == 0xC0) { need = 1; cp = b0 & 0x1F; min = 0x80; }
    else if ((b0 & 0xF0) == 0xE0) { need = 2; cp = b0 & 0x0F; min = 0x800; }
    else if ((b0 & 0xF8) == 0xF0) { need = 3; cp = b0 & 0x07; min = 0x10000; }
    else return {kReplacement, 1};

    if (s.size() <= need) return {kReplacement, 1};
    for (size_t i = 1; i <= need; i++) {
        const auto b = static_cast<unsigned char>(s[i]);
        if ((b & 0xC0) != 0x80) return {kReplacement, 1};
        cp = (cp << 6) | (b & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return {kReplacement, 1};
    return {cp, need + 1};
}

void AppendRune(std::string& out, char32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

size_t RuneCount(std::string_view s) {
    size_t n = 0;
    while (!s.empty()) {
        s.remove_prefix(DecodeRune(s).size);
        n++;
    }
    return n;
}

}  // namespace hdTrans::text
