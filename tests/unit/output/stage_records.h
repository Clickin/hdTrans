#pragma once
// Reading and writing the stage record files of an expectation set
// ($HDTRANS_GOLDEN/stages/<set>.<stage>.tsv): one record per line, tab-separated,
// text columns escaped by EscapeStageText.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "core/text/utf8.h"
#include "hdtest.h"

namespace stagerecords {

// Backslash, tab, line feed and carriage return as \\, \t, \n, \r; the other C0
// controls, DEL and each byte of an invalid UTF-8 sequence as \xNN (lower case hex).
inline std::string EscapeStageText(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const auto r = hdTrans::text::DecodeRune(s.substr(i));
        const auto b = static_cast<unsigned char>(s[i]);
        if (r.cp == U'\\') {
            out += "\\\\";
        } else if (r.cp == U'\t') {
            out += "\\t";
        } else if (r.cp == U'\n') {
            out += "\\n";
        } else if (r.cp == U'\r') {
            out += "\\r";
        } else if (r.cp < 0x20 || r.cp == 0x7F || (r.cp == hdTrans::text::kReplacement && r.size == 1)) {
            constexpr char kHex[] = "0123456789abcdef";
            out += "\\x";
            out += kHex[b >> 4];
            out += kHex[b & 15];
        } else {
            out += s.substr(i, r.size);
        }
        i += r.size;
    }
    return out;
}

inline std::string UnescapeStageText(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out += s[i];
            continue;
        }
        const char c = s[++i];
        switch (c) {
            case 't': out += '\t'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 'x':
                if (i + 2 < s.size()) {
                    out += static_cast<char>(std::strtol(std::string(s.substr(i + 1, 2)).c_str(), nullptr, 16));
                    i += 2;
                }
                break;
            default: out += c; break;
        }
    }
    return out;
}

inline std::vector<std::string_view> Split(std::string_view s, char sep) {
    std::vector<std::string_view> parts;
    for (size_t start = 0;;) {
        const size_t end = s.find(sep, start);
        if (end == std::string_view::npos) {
            parts.push_back(s.substr(start));
            return parts;
        }
        parts.push_back(s.substr(start, end - start));
        start = end + 1;
    }
}

inline bool ReadFile(const std::string& path, std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    return true;
}

// The records of a set's stage file under the #CASE / #LINE header that precedes
// them; a header with no records is a block of its own.
struct Block {
    std::string_view header;
    std::vector<std::string_view> records;
};

inline std::vector<Block> Blocks(std::string_view file) {
    std::vector<Block> blocks;
    for (const auto line : Split(file, '\n')) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            blocks.push_back({line, {}});
        } else if (!blocks.empty()) {
            blocks.back().records.push_back(line);
        }
    }
    return blocks;
}

// The explicitly configured expectation folder.
using hdtest::GoldenDir;

}  // namespace stagerecords
