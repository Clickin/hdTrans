#include "hdTrans/pipeline/normalize.h"
#include "hdTrans/pipeline/pattern.h"

namespace hdTrans::pipeline::internal {

namespace {

// Stems that fiction writes in katakana for emphasis and the dictionary does not have,
// with their hiragana.
struct KataStem {
    std::string_view kata;
    std::string_view hira;
};
constexpr KataStem kKataStems[] = {
    {"イラつ", "いらつ"}, {"ショボ", "しょぼ"}, {"ウルサ", "うるさ"}, {"ウザ", "うざ"}, {"ダサ", "ださ"},
    {"エグ", "えぐ"},     {"ダル", "だる"},     {"ヒド", "ひど"},     {"コワ", "こわ"}, {"ウマ", "うま"},
    {"マズ", "まず"},     {"セコ", "せこ"},     {"ムズ", "むず"},     {"ハズ", "はず"},
};

// The first characters that let a text hold a katakana stem; ハ is not among them.
constexpr std::u32string_view kKataStemStarts = U"イショウダエヒコマセム";

// Group 1 a stem of kKataStems in their order, group 2 its ending.
bool KataStemPattern(std::string_view s, size_t pos, Match& m) {
    for (const KataStem& k : kKataStems) {
        size_t p = pos;
        if (!Lit(s, p, k.kata)) continue;
        for (std::string_view e : {"い", "く", "かっ", "かろ", "け", "さ", "すぎ", "そう", "む", "み", "ま", "め", "ん"}) {
            size_t q = p;
            if (!Lit(s, q, e)) continue;
            m.group[1] = Span(s, pos, p);
            m.group[2] = Span(s, p, q);
            m.end = q;
            return true;
        }
    }
    return false;
}

// ([^\s「『（、。！？]じゃ|[良よ]く)ね[ぇえーっ]*([？?]|の|か|よ|だろ|って)
bool JaneQuestion(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        if (!Lit(s, p, "ね")) return false;
        return Repeat(s, p, InSet(U"ぇえーっ"), 0, kUnbounded, [&](size_t q) {
            size_t r = q;
            if (OneOf(s, r, U"？?")) {
                m.group[2] = Span(s, q, r);
                m.end = r;
                return true;
            }
            for (std::string_view w : {"の", "か", "よ", "だろ", "って"}) {
                r = q;
                if (!Lit(s, r, w)) continue;
                m.group[2] = Span(s, q, r);
                m.end = r;
                return true;
            }
            return false;
        });
    };
    size_t p = pos;
    if (NoneOf(s, p, U"\t\n\f\r 「『（、。！？") && Lit(s, p, "じゃ") && rest(p)) return true;
    p = pos;
    return OneOf(s, p, U"良よ") && Lit(s, p, "く") && rest(p);
}

// ([はも])ね[ぇえー]+(だろ|でしょ|よ|って|し)
bool ParticleNee(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"はも")) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, "ね")) return false;
    return Repeat(s, p, InSet(U"ぇえー"), 1, kUnbounded, [&](size_t q) {
        for (std::string_view w : {"だろ", "でしょ", "よ", "って", "し"}) {
            size_t r = q;
            if (!Lit(s, r, w)) continue;
            m.group[2] = Span(s, q, r);
            m.end = r;
            return true;
        }
        return false;
    });
}

// A rune of first, then lit, then one of the words: groups 1 and 2.
template <size_t N>
bool CharLitWord(std::string_view s, size_t pos, Match& m, std::u32string_view first, std::string_view lit,
                 const std::string_view (&words)[N]) {
    size_t p = pos;
    if (!OneOf(s, p, first)) return false;
    m.group[1] = Span(s, pos, p);
    if (!Lit(s, p, lit)) return false;
    for (std::string_view w : words) {
        size_t q = p;
        if (!Lit(s, q, w)) continue;
        m.group[2] = Span(s, p, q);
        m.end = q;
        return true;
    }
    return false;
}

constexpr std::string_view kTeruAfter[] = {"じゃ", "だろ"};
constexpr std::string_view kIruAfter[] = {"の", "だ", "じゃ"};

// ([てで])ん(じゃ|だろ)
bool ContractedTeru(std::string_view s, size_t pos, Match& m) {
    return CharLitWord(s, pos, m, U"てで", "ん", kTeruAfter);
}

// ([にでもがは])いん(の|だ|じゃ)
bool ContractedIru(std::string_view s, size_t pos, Match& m) {
    return CharLitWord(s, pos, m, U"にでもがは", "いん", kIruAfter);
}

// One rune of set or the end of s at p as group 2, ending the match.
bool EndGroup(std::string_view s, size_t p, std::u32string_view set, Match& m) {
    size_t q = p;
    if (!OneOf(s, q, set) && p != s.size()) return false;
    m.group[2] = Span(s, p, q);
    m.end = q;
    return true;
}

// ([うくぐすつぬぶむる])んじゃない([！!」』]|$)
bool ProhibitionJanai(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!OneOf(s, p, U"うくぐすつぬぶむる")) return false;
    m.group[1] = Span(s, pos, p);
    return Lit(s, p, "んじゃない") && EndGroup(s, p, U"！!」』", m);
}

// (..[いたる])じゃない([！!。、…‥」』]|$)
bool AppealJanai(std::string_view s, size_t pos, Match& m) {
    auto notNewline = [](char32_t c) { return c != U'\n'; };
    size_t p = pos;
    if (!Take(s, p, notNewline) || !Take(s, p, notNewline) || !OneOf(s, p, U"いたる")) return false;
    m.group[1] = Span(s, pos, p);
    return Lit(s, p, "じゃない") && EndGroup(s, p, U"！!。、…‥」』", m);
}

// The words ending in い after which じゃない is the negative (嫌いじゃない), and てる.
bool NotAppeal(std::string_view stem) {
    for (std::string_view w : {"嫌い", "きらい", "きれい", "みたい", "くらい", "ぐらい", "せい", "てる", "でる"}) {
        if (stem.ends_with(w)) return true;
    }
    return false;
}

// (怖|寒|…|こわ)っ+([！!？?。、…‥」』]|$)
bool ClippedAdjective(std::string_view s, size_t pos, Match& m) {
    for (std::string_view w : {"怖", "寒", "痛", "熱", "暑", "重", "早", "速", "高", "狭", "臭", "硬", "固", "甘",
                               "眩し", "遅", "深", "長", "短", "近", "遠", "細", "太", "汚", "酷", "小さ", "美味し",
                               "旨", "凄", "弱", "強", "安", "暗", "広", "軽", "薄", "濃", "冷た", "すご", "うざ",
                               "きも", "ださ", "えぐ", "ひど", "でか", "かわい", "しょぼ", "さむ", "こわ"}) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        m.group[1] = Span(s, pos, p);
        const bool ok = Repeat(s, p, InSet(U"っ"), 1, kUnbounded,
                               [&](size_t q) { return EndGroup(s, q, U"！!？?。、…‥」』", m); });
        if (ok) return true;
    }
    return false;
}

constexpr std::u32string_view kNegativeEnd = U"！!？?。、…‥」』ぞよわがしけっ";

// ([一-龥々][らわかがまば]|許さ|わから|しら|かまわ)ん([！!？?。、…‥」』ぞよわがしけっ]|$)
bool NegativeN(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        return Lit(s, p, "ん") && EndGroup(s, p, kNegativeEnd, m);
    };
    size_t p = pos;
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"らわかがまば") && rest(p)) return true;
    for (std::string_view w : {"許さ", "わから", "しら", "かまわ"}) {
        p = pos;
        if (Lit(s, p, w) && rest(p)) return true;
    }
    return false;
}

// (でき|得|足り|られ|させ|言え|見え|聞こえ)ん([！!？?。、…‥」』ぞよわがしけっ]|$)
bool NegativeE(std::string_view s, size_t pos, Match& m) {
    for (std::string_view w : {"でき", "得", "足り", "られ", "させ", "言え", "見え", "聞こえ"}) {
        size_t p = pos;
        if (!Lit(s, p, w)) continue;
        m.group[1] = Span(s, pos, p);
        if (Lit(s, p, "ん") && EndGroup(s, p, kNegativeEnd, m)) return true;
    }
    return false;
}

constexpr char kDekinu[] = "できぬ";

bool Dekinu(std::string_view s, size_t pos, Match& m) {
    size_t p = pos;
    if (!Lit(s, p, kDekinu)) return false;
    m.end = p;
    return true;
}

bool ContainsAny(std::string_view s, std::u32string_view set) {
    for (size_t i = 0; i < s.size();) {
        const text::Rune r = At(s, i);
        if (In(r.cp, set)) return true;
        i += r.size;
    }
    return false;
}

bool Contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

std::string Group1Plus(const Match& m, std::string_view mid) { return Cat({m.group[1], mid, m.group[2]}); }

}  // namespace

std::string SpokenForms(std::string_view in) {
    std::string text(in);
    if (ContainsAny(text, kKataStemStarts)) {
        text = ReplaceAll(text, KataStemPattern, [](const Match& m) {
            const Match g = Rematch(m, KataStemPattern);
            std::string_view hira;
            for (const KataStem& k : kKataStems) {
                if (k.kata == g.group[1]) hira = k.hira;
            }
            return Cat({hira, g.group[2]});
        });
    }
    if (Contains(text, "ね")) {
        text = ReplaceAll(text, JaneQuestion, [](const Match& m) { return Group1Plus(m, "ない"); });
        text = ReplaceAll(text, ParticleNee, [](const Match& m) { return Group1Plus(m, "ない"); });
    }
    if (Contains(text, "ん")) {
        text = ReplaceAll(text, ContractedTeru, [](const Match& m) { return Group1Plus(m, "るん"); });
        text = ReplaceAll(text, ContractedIru, [](const Match& m) {
            const Match g = Rematch(m, ContractedIru);
            if (g.group[2] == "の") return Cat({g.group[1], "いるの"});
            return Cat({g.group[1], "いるん", g.group[2]});
        });
    }
    if (Contains(text, "じゃない")) {
        text = ReplaceAll(text, ProhibitionJanai, [](const Match& m) { return Group1Plus(m, "なよ"); });
        text = ReplaceAll(text, AppealJanai, [](const Match& m) {
            const Match g = Rematch(m, AppealJanai);
            if (NotAppeal(g.group[1])) return std::string(m.group[0]);
            return Group1Plus(g, "じゃん");
        });
    }
    if (Contains(text, "っ")) {
        text = ReplaceAll(text, ClippedAdjective, [](const Match& m) { return Group1Plus(m, "い"); });
    }
    if (Contains(text, "ん")) {
        text = ReplaceAll(text, NegativeN, [](const Match& m) { return Group1Plus(m, "ない"); });
        text = ReplaceAll(text, NegativeE, [](const Match& m) { return Group1Plus(m, "ない"); });
    }
    return ReplaceAll(text, Dekinu, [](const Match&) { return std::string("できない"); });
}

}  // namespace hdTrans::pipeline::internal
