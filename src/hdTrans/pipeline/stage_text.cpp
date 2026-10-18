#include "hdTrans/pipeline/stage_text.h"

#include "hdTrans/text/utf8.h"

namespace hdTrans::pipeline {

std::string EscapeStageText(std::string_view s) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        if (r.cp == U'\\') {
            out += "\\\\";
        } else if (r.cp == U'\t') {
            out += "\\t";
        } else if (r.cp == U'\n') {
            out += "\\n";
        } else if (r.cp == U'\r') {
            out += "\\r";
        } else if (r.cp < 0x20 || r.cp == 0x7F || (r.cp == text::kReplacement && r.size == 1)) {
            const auto b = static_cast<unsigned char>(s[i]);
            out += "\\x";
            out.push_back(kHex[b >> 4]);
            out.push_back(kHex[b & 15]);
        } else {
            out.append(s.substr(i, r.size));
        }
        i += r.size;
    }
    return out;
}

}  // namespace hdTrans::pipeline
