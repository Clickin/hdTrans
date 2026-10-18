#include "hdTrans/pipeline/normalize.h"
#include "hdTrans/pipeline/pattern.h"

namespace hdTrans::pipeline::internal {

namespace {

// An optional part at pos (part(pos, rest) tries it and then rest), taken when it lets
// rest match, skipped otherwise.
template <class Part, class Rest>
bool Optional(size_t pos, Part part, Rest rest) {
    return part(pos, rest) || rest(pos);
}

// 《([^《》]{1,30})》 at p, then the end of the match.
bool Reading(std::string_view s, size_t p, Match& m) {
    if (!Lit(s, p, "《")) return false;
    return Repeat(s, p, [](char32_t c) { return c != U'《' && c != U'》'; }, 1, 30, [&](size_t q) {
        size_t r = q;
        if (!Lit(s, r, "》")) return false;
        m.end = r;
        return true;
    });
}

// [|｜]([^|｜《》]{1,20})《([^《》]{1,30})》, base as group 1
bool BarRuby(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"|｜")) return false;
    return Repeat(s, p, [](char32_t c) { return !In(c, U"|｜《》"); }, 1, 20, [&](size_t q) {
        m.group[1] = Span(s, p, q);
        return Reading(s, q, m);
    });
}

// ([一-龥々〆ヶ]+)《([^《》]{1,30})》, base as group 3
bool KanjiRuby(std::string_view s, size_t pos, Match& m) {
    return Repeat(s, pos, [](char32_t c) { return IsKanjiClass(c) || c == U'〆' || c == U'ヶ'; }, 1, kUnbounded,
                  [&](size_t q) {
                      m.group[3] = Span(s, pos, q);
                      return Reading(s, q, m);
                  });
}

// <rp>[^<]*</rp> at p, then rest.
template <class Rest>
bool Paren(std::string_view s, size_t p, Rest rest) {
    if (!Lit(s, p, "<rp>")) return false;
    return Repeat(s, p, [](char32_t c) { return c != U'<'; }, 0, kUnbounded, [&](size_t q) {
        return Lit(s, q, "</rp>") && rest(q);
    });
}

// <ruby>(?:<rb>)?([^<]{1,20})(?:</rb>)?(?:<rp>[^<]*</rp>)?<rt>([^<]{1,30})</rt>(?:<rp>[^<]*</rp>)?</ruby>,
// base as group 5
bool HtmlRuby(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, "<ruby>")) return false;
    auto notOpen = [](char32_t c) { return c != U'<'; };
    auto end = [&](size_t e) {
        if (!Lit(s, e, "</ruby>")) return false;
        m.end = e;
        return true;
    };
    auto annotation = [&](size_t a) {
        if (!Lit(s, a, "<rt>")) return false;
        return Repeat(s, a, notOpen, 1, 30, [&](size_t b) {
            if (!Lit(s, b, "</rt>")) return false;
            return Optional(b, [&](size_t c, auto rest) { return Paren(s, c, rest); }, end);
        });
    };
    auto base = [&](size_t b) {
        return Repeat(s, b, notOpen, 1, 20, [&](size_t c) {
            m.group[5] = Span(s, b, c);
            auto afterRb = [&](size_t d) {
                return Optional(d, [&](size_t e, auto rest) { return Paren(s, e, rest); }, annotation);
            };
            return Optional(
                c, [&](size_t d, auto rest) { return Lit(s, d, "</rb>") && rest(d); }, afterRb);
        });
    };
    return Optional(p, [&](size_t q, auto rest) { return Lit(s, q, "<rb>") && rest(q); }, base);
}

bool RubyForm(std::string_view s, size_t pos, Match& m) {
    if (BarRuby(s, pos, m)) return true;
    m = Match{};
    if (KanjiRuby(s, pos, m)) return true;
    m = Match{};
    return HtmlRuby(s, pos, m);
}

}  // namespace

std::string StripRuby(std::string_view line) {
    if (line.find("《") == std::string_view::npos && line.find('<') == std::string_view::npos) {
        return std::string(line);
    }
    return ReplaceAll(line, RubyForm, [](const Match& m) { return Cat({m.group[1], m.group[3], m.group[5]}); });
}

}  // namespace hdTrans::pipeline::internal
