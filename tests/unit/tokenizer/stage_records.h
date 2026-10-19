#pragma once
// Reading and writing stage records: tab-separated lines whose text columns escape
// \ TAB LF CR as \\ \t \n \r, the other C0 controls, DEL and every byte of an invalid
// UTF-8 sequence as \xNN, and nothing else.
#include <string>
#include <string_view>

#include "core/text/utf8.h"

namespace stagetest {

inline std::string EscapeStageText(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const hdTrans::text::Rune r = hdTrans::text::DecodeRune(s.substr(i));
        const auto b = static_cast<unsigned char>(s[i]);
        if (r.cp == '\\') {
            out += "\\\\";
        } else if (r.cp == '\t') {
            out += "\\t";
        } else if (r.cp == '\n') {
            out += "\\n";
        } else if (r.cp == '\r') {
            out += "\\r";
        } else if (r.cp < 0x20 || r.cp == 0x7f || (r.cp == hdTrans::text::kReplacement && r.size == 1)) {
            static const char kHex[] = "0123456789abcdef";
            out += "\\x";
            out += kHex[b >> 4];
            out += kHex[b & 15];
        } else {
            out.append(s.substr(i, r.size));
        }
        i += r.size;
    }
    return out;
}

inline std::string UnescapeStageText(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    auto hex = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out += s[i];
            continue;
        }
        const char c = s[++i];
        if (c == 't') {
            out += '\t';
        } else if (c == 'n') {
            out += '\n';
        } else if (c == 'r') {
            out += '\r';
        } else if (c == 'x' && i + 2 < s.size()) {
            out += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace stagetest
