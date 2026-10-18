#include "hdTrans/text/lines.h"

#include "hdTrans/text/utf8.h"

namespace hdTrans::text {

std::vector<std::string_view> Lines(std::string_view text) {
    std::vector<std::string_view> out;
    for (;;) {
        const std::size_t end = text.find('\n');
        out.push_back(text.substr(0, end));
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return out;
}

std::vector<std::string_view> TranslationPieces(std::string_view line) {
    if (line.size() <= kMaxLineBytes) return {line};
    // Smaller windows also bound candidate storage for punctuation-free text.
    constexpr std::size_t kWindowBytes = 16 * 1024;
    std::vector<std::string_view> out;
    while (line.size() > kWindowBytes) {
        std::size_t end = 0, sentence = 0, space = 0;
        while (end < line.size()) {
            const Rune rune = DecodeRune(line.substr(end));
            if (end + rune.size > kWindowBytes) break;
            end += rune.size;
            switch (rune.cp) {
            case U'。': case U'！': case U'？': case U'!': case U'?':
            case U'\n': case U'\r': case U'\v': case U'\f':
            case U'\u0085': case U'\u2028': case U'\u2029':
                sentence = end;
                break;
            case U' ': case U'\t': case U'　':
                space = end;
                break;
            default:
                break;
            }
        }
        const std::size_t cut = sentence != 0 ? sentence : space != 0 ? space : end;
        out.push_back(line.substr(0, cut));
        line.remove_prefix(cut);
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

}  // namespace hdTrans::text
