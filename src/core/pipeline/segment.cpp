#include "core/pipeline/segment.h"

#include "core/pipeline/runes.h"
#include "core/text/unicode.h"
#include "core/text/utf8.h"

namespace hdTrans::pipeline {

namespace internal {

namespace {

constexpr int kChunkBefore = 40;
constexpr int kChunkAfter = 10;

constexpr std::string_view kConjunctiveEndings[] = {"て",   "ば",   "たら", "だら",     "ても",   "でも",  "けど",
                                                     "けれど", "けれども", "から", "ので", "のに", "ながら"};

// Endings that end a clause only after one of the given characters.
struct ConjunctiveAfter {
    std::string_view ending;
    std::u32string_view before;
};

constexpr ConjunctiveAfter kConjunctiveAfter[] = {
    {"で", U"んい"},        // 読んで, 泳いで, ないで (not the particle で)
    {"し", U"いるだすうく"},  // 行くし, いいし, だし
    {"が", U"たいるうすだん"},  // 思ったが, ですが, ませんが
};

bool IsSentenceEnd(char32_t r) { return RuneIn(r, U"。．！？!?.\n\r"); }

// A space, an ideographic space or a tab.
bool IsBlank(char32_t r) { return r == U' ' || r == U'　' || r == U'\t'; }

std::string_view TrimLeftRunes(std::string_view s, std::u32string_view set) {
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (!RuneIn(r.cp, set)) break;
        s.remove_prefix(r.size);
    }
    return s;
}

// The characters other than white space of text from piece k, byte from, before the
// first sentence end; a span counts as one.
int CharsBeforeEnd(const output::Pieces& text, size_t k, size_t from) {
    int n = 0;
    for (; k < text.size(); k++, from = 0) {
        if (text[k].span != output::SpanText) {
            n++;
            continue;
        }
        for (std::string_view s = std::string_view(text[k].text).substr(from); !s.empty();) {
            const text::Rune r = text::DecodeRune(s);
            if (IsSentenceEnd(r.cp)) return n;
            if (!text::IsSpace(r.cp)) n++;
            s.remove_prefix(r.size);
        }
    }
    return n;
}

}  // namespace

bool KanaSpace(std::string_view s, size_t i) {
    const char32_t last = LastRune(s.substr(0, i)).cp;
    std::string_view rest = s.substr(i);
    while (!rest.empty() && rest[0] == ' ') rest.remove_prefix(1);
    const char32_t first = text::DecodeRune(rest).cp;
    if (!IsHiraganaRune(last) || !IsJapaneseRune(first)) return false;
    return RuneIn(last, U"ねよさわぞぜ") ||
           (IsKanjiRune(first) && !RuneIn(last, U"いただるうくすつぬむぶぐなのはがをにでともへやか"));
}

bool Conjunctive(std::string_view s) {
    for (const std::string_view e : kConjunctiveEndings) {
        if (s.ends_with(e)) return true;
    }
    for (const ConjunctiveAfter& p : kConjunctiveAfter) {
        if (s.ends_with(p.ending) && RuneIn(LastRune(s.substr(0, s.size() - p.ending.size())).cp, p.before)) return true;
    }
    return false;
}

bool BetweenWords(std::string_view s, size_t i) {
    auto word = [](char32_t r) { return r != text::kReplacement && !IsBlank(r) && r != U'\n' && r != U'\r'; };
    const text::Rune last = LastRune(s.substr(0, i));
    const char32_t before = LastRune(s.substr(0, i - last.size)).cp;
    const std::string_view rest = TrimLeftRunes(s.substr(i), U" 　\t");
    const text::Rune first = text::DecodeRune(rest);
    const char32_t next = text::DecodeRune(rest.substr(first.size)).cp;
    return word(last.cp) && word(first.cp) && (word(before) || word(next));
}

}  // namespace internal

std::vector<Chunk> Segment(const output::Pieces& pieces) {
    using namespace internal;
    std::vector<Chunk> parts;
    Chunk part;
    int count = 0;
    for (size_t k = 0; k < pieces.size(); k++) {
        const output::Piece& p = pieces[k];
        if (p.span != output::SpanText) {
            part.push_back(p);
            continue;
        }
        const std::string_view t = p.text;
        size_t start = 0;
        for (size_t i = 0; i < t.size();) {
            const text::Rune r = text::DecodeRune(t.substr(i));
            if ((r.cp == U'　' || (r.cp == U' ' && KanaSpace(t, i))) && i > start && BetweenWords(t, i)) {
                output::Add(part, t.substr(start, i - start));
                parts.push_back(std::move(part));
                part.clear();
                start = i;
                count = 0;
            } else if (IsSentenceEnd(r.cp)) {
                count = 0;
            } else if (r.cp == U'、') {
                const size_t end = i + r.size;
                if (count >= kChunkBefore && Conjunctive(t.substr(0, i)) && CharsBeforeEnd(pieces, k, end) >= kChunkAfter) {
                    output::Add(part, t.substr(start, end - start));
                    parts.push_back(std::move(part));
                    part.clear();
                    start = end;
                    count = 0;
                }
            } else if (IsJapaneseRune(r.cp)) {
                count++;
            }
            i += r.size;
        }
        output::Add(part, t.substr(start));
    }
    parts.push_back(std::move(part));
    for (size_t i = 1; i < parts.size(); i++) {
        Chunk& c = parts[i];
        if (c.empty() || c[0].span != output::SpanText) continue;
        const std::string_view rest = TrimLeftRunes(c[0].text, U" 　\t");
        if (!rest.empty()) {
            c[0].text = std::string(rest);
        } else {
            c.erase(c.begin());
        }
    }
    return parts;
}

}  // namespace hdTrans::pipeline
