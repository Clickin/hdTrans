#include "hdTrans/text/lines.h"

#include <array>

#include "hdTrans/text/utf8.h"

namespace hdTrans::text {
namespace {

constexpr std::u32string_view kOpen = U"「『（《〈【〔［｛([{<“‘\"'";
constexpr std::u32string_view kClose = U"」』）》〉】〕］｝)]}>”’\"'";
static_assert(kOpen.size() == kClose.size());

bool SentenceEnd(char32_t cp) {
    switch (cp) {
    case U'。': case U'！': case U'？': case U'!': case U'?':
    case U'\n': case U'\r': case U'\v': case U'\f':
    case U'\u0085': case U'\u2028': case U'\u2029':
        return true;
    default:
        return false;
    }
}

bool Space(char32_t cp) { return cp == U' ' || cp == U'\t' || cp == U'　'; }

struct BoundaryState {
    std::array<std::size_t, kOpen.size()> depth{};
    std::size_t open = 0;
    bool escaped = false, sentenceTail = false;

    void Add(char32_t cp) {
        const bool literalQuote = escaped && (cp == U'\"' || cp == U'\'');
        escaped = cp == U'\\' && !escaped;
        bool closed = false;
        if (!literalQuote) {
            for (std::size_t i = 0; i < kOpen.size(); i++) {
                if (cp == kClose[i] && depth[i] != 0) {
                    depth[i]--;
                    open--;
                    closed = true;
                    break;
                }
                if (cp == kOpen[i]) {
                    depth[i]++;
                    open++;
                    break;
                }
            }
        }
        sentenceTail = SentenceEnd(cp) || ((closed || Space(cp)) && sentenceTail);
    }
};

}  // namespace

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
    BoundaryState state;
    while (line.size() > kWindowBytes) {
        std::size_t end = 0, sentence = 0, space = 0;
        std::size_t outside = 0, outsideSentence = 0, outsideSpace = 0;
        BoundaryState scanned = state;
        while (end < line.size()) {
            const Rune rune = DecodeRune(line.substr(end));
            if (end + rune.size > kWindowBytes) break;
            end += rune.size;
            scanned.Add(rune.cp);
            if (SentenceEnd(rune.cp)) sentence = end;
            if (Space(rune.cp)) space = end;
            if (scanned.open == 0) {
                outside = end;
                if (scanned.sentenceTail && !Space(rune.cp)) outsideSentence = end;
                if (Space(rune.cp)) outsideSpace = end;
            }
        }
        const std::size_t cut = outsideSentence != 0 ? outsideSentence : outsideSpace != 0 ? outsideSpace :
                                outside != 0 ? outside : sentence != 0 ? sentence : space != 0 ? space : end;
        // Carry only the state at the selected cut, not at the end of the scan.
        for (std::size_t i = 0; i < cut;) {
            const Rune rune = DecodeRune(line.substr(i));
            state.Add(rune.cp);
            i += rune.size;
        }
        out.push_back(line.substr(0, cut));
        line.remove_prefix(cut);
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

}  // namespace hdTrans::text
