#pragma once
// Reading and writing stage records: tab-separated lines whose text columns escape
// \ TAB LF CR as \\ \t \n \r, the other C0 controls, DEL and every byte of an invalid
// UTF-8 sequence as \xNN, and nothing else.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/text/utf8.h"

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

// Splits a line at tabs.
inline std::vector<std::string_view> Columns(std::string_view line) {
    std::vector<std::string_view> cols;
    for (;;) {
        const size_t tab = line.find('\t');
        cols.push_back(line.substr(0, tab));
        if (tab == std::string_view::npos) return cols;
        line.remove_prefix(tab + 1);
    }
}

// Splits a file at LF; a final LF ends the last line.
inline std::vector<std::string_view> Lines(std::string_view text) {
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const size_t lf = text.find('\n');
        lines.push_back(text.substr(0, lf));
        if (lf == std::string_view::npos) break;
        text.remove_prefix(lf + 1);
    }
    return lines;
}

inline bool ReadFile(const std::string& path, std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    return true;
}

// $HDTRANS_GOLDEN/stages/, by default hdtrans-golden next to the repository. Empty when
// the set is absent or incomplete (it is complete once its MANIFEST exists).
inline std::string StagesDir() {
    std::string dir;
    if (const char* g = std::getenv("HDTRANS_GOLDEN"); g != nullptr && *g != 0) {
        dir = g;
    } else {
        std::string f = __FILE__;
        const size_t slash = f.find_last_of("/\\");
        dir = (slash == std::string::npos ? std::string(".") : f.substr(0, slash)) + "/../../../../hdtrans-golden";
    }
    dir += "/stages/";
    std::FILE* m = std::fopen((dir + "MANIFEST").c_str(), "rb");
    if (m == nullptr) return {};
    std::fclose(m);
    return dir;
}

}  // namespace stagetest
