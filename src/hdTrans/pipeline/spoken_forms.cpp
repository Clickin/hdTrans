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

bool NegativeNee(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        if (!Lit(s, p, "ね")) return false;
        return Repeat(s, p, InSet(U"ぇえー"), 1, kUnbounded, [&](size_t q) {
            m.end = q;
            return true;
        });
    };
    size_t p = pos;
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"らわさまばが") && rest(p)) return true;
    for (std::string_view stem : {"動", "書", "聞", "行", "効", "届", "働", "開", "抜", "泣", "驚", "歩", "気付", "敵わ"}) {
        p = pos;
        if (Lit(s, p, stem) && Lit(s, p, "か") && rest(p)) return true;
    }
    for (std::string_view stem : {"待", "持", "立", "勝", "打", "育", "保"}) {
        p = pos;
        if (Lit(s, p, stem) && Lit(s, p, "た") && rest(p)) return true;
    }
    for (std::string_view stem : {"死な", "かまわ", "構わ", "わから", "わかん", "分かん", "しら", "いら", "たまら", "たまん",
                                  "つまら", "つまん", "くだら", "くだん", "でき", "出来", "すま", "仕方", "しかた", "しょうが",
                                  "しょーが", "ありえ", "あり得", "見え", "聞こえ"}) {
        p = pos;
        if (Lit(s, p, stem) && rest(p)) return true;
    }
    for (std::string_view stem : {"面白", "悪", "痛", "怖", "寒", "熱", "暑", "強", "弱", "早", "遅", "高", "安", "重", "軽",
                                  "長", "短", "近", "遠", "多", "少", "狭", "広", "硬", "甘", "辛", "うま", "すご", "かわい", "おもしろ"}) {
        p = pos;
        if (Lit(s, p, stem) && Lit(s, p, "く") && rest(p)) return true;
    }
    return false;
}

std::string ReplaceNegativeNee(const Match& m) {
    const Match g = Rematch(m, NegativeNee);
    std::string_view stem = g.group[1];
    if (stem == "わかん") stem = "わから";
    else if (stem == "分かん") stem = "分から";
    else if (stem == "たまん") stem = "たまら";
    else if (stem == "つまん") stem = "つまら";
    else if (stem == "くだん") stem = "くだら";
    return Cat({stem, "ない"});
}

bool KansaiNegative(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        if (!Lit(s, p, "へん")) return false;
        const size_t ending = p;
        if (Lit(s, p, "かった")) m.group[2] = Span(s, ending, p);
        else m.group[2] = {};
        m.end = p;
        return true;
    };
    size_t p = pos;
    if (Take(s, p, IsKanjiClass) && OneOf(s, p, U"わかがさたなまば") && rest(p)) return true;
    for (std::string_view stem : {"知", "入", "帰", "要", "走", "乗", "取", "作", "守", "足", "分か", "変わ"}) {
        p = pos;
        if (Lit(s, p, stem) && Lit(s, p, "ら") && rest(p)) return true;
    }
    p = pos;
    if (OneOf(s, p, U"てで") && rest(p)) return true;
    for (std::string_view stem : {"あら", "あれ", "せえ", "でき", "出来", "わから", "しれ", "知れ", "おら", "いら"}) {
        p = pos;
        if (Lit(s, p, stem) && rest(p)) return true;
    }
    return false;
}

std::string ReplaceKansaiNegative(const Match& m) {
    const Match g = Rematch(m, KansaiNegative);
    std::string_view stem = g.group[1];
    if (stem == "あら" || stem == "あれ") stem = {};
    else if (stem == "せえ") stem = "し";
    return Cat({stem, g.group[2].empty() ? "ない" : "なかった"});
}

bool ContractedToru(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        for (std::string_view ending : {"る", "った", "らん", "れ"}) {
            size_t q = p;
            if (!Lit(s, q, ending)) continue;
            const size_t end = q;
            if (!OneOf(s, q, U"！!？?。、…‥」』ぞよわがけでなのやっん") && end != s.size()) continue;
            m.group[2] = Span(s, p, end);
            m.group[3] = Span(s, end, q);
            m.end = q;
            return true;
        }
        return false;
    };
    size_t p = pos;
    if (OneOf(s, p, U"いっし") && Lit(s, p, "と") && rest(p)) return true;
    p = pos;
    return Lit(s, p, "んど") && rest(p);
}

std::string ReplaceContractedToru(const Match& m) {
    const Match g = Rematch(m, ContractedToru);
    const bool voiced = g.group[1].ends_with("ど");
    const std::string_view contracted = voiced ? "ど" : "と";
    const std::string_view stem = g.group[1].substr(0, g.group[1].size() - contracted.size());
    std::string_view ending;
    if (g.group[2] == "る") ending = "いる";
    else if (g.group[2] == "った") ending = "いた";
    else if (g.group[2] == "らん") ending = "いない";
    else if (g.group[2] == "れ") ending = "いろ";
    return Cat({stem, voiced ? "で" : "て", ending, g.group[3]});
}

bool ExclamationNa(std::string_view s, size_t pos, Match& m) {
    auto rest = [&](size_t p) {
        m.group[1] = Span(s, pos, p);
        if (!Lit(s, p, "な")) return false;
        return Repeat(s, p, InSet(U"ぁあー～"), 0, kUnbounded, [&](size_t q) {
            size_t r = q;
            if (!OneOf(s, r, U"！!？?。…‥」』）") && q != s.size()) return false;
            m.group[2] = Span(s, p, q);
            m.group[3] = Span(s, q, r);
            m.end = r;
            return true;
        });
    };
    constexpr std::u32string_view excluded = U"\t\n\f\r 、。！？!?…‥「」『』";
    auto afterRun = [&](size_t p, size_t min, size_t max) {
        // Try shorter rune runs before longer ones.
        for (size_t count = 0;; ++count) {
            if (count >= min) {
                size_t q = p;
                if (OneOf(s, q, U"うくぐすつぬぶむる") && rest(q)) return true;
            }
            if (count == max || !NoneOf(s, p, excluded)) return false;
        }
    };
    size_t p = pos;
    if (NoneOf(s, p, U"\t\n\f\r 、。！？!?…‥「」『』（") && Lit(s, p, "が") && afterRun(p, 1, 8)) return true;
    p = pos;
    if (Lit(s, p, "よく") && afterRun(p, 0, 4)) return true;
    for (std::string_view stem : {"気もする", "そうなる", "ことになる", "事になる", "なかなかやる", "違う", "困る", "助かる", "見える",
                                  "聞こえる", "落ち着く"}) {
        p = pos;
        if (Lit(s, p, stem) && rest(p)) return true;
    }
    return false;
}

std::string ReplaceExclamationNa(const Match& m) {
    const Match g = Rematch(m, ExclamationNa);
    for (std::string_view ending : {"言う", "よう", "そう", "です", "ます"}) {
        if (g.group[1].ends_with(ending)) return std::string(m.group[0]);
    }
    size_t trim = 0;
    while (OneOf(g.group[2], trim, U"ぁあ")) {}
    return Cat({g.group[1], "なあ", g.group[2].substr(trim), g.group[3]});
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

std::string SpokenForms(std::string_view in, bool exclamationNa, bool dialectForms) {
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
        if (dialectForms) text = ReplaceAll(text, NegativeNee, ReplaceNegativeNee);
    }
    if (dialectForms && Contains(text, "へん")) text = ReplaceAll(text, KansaiNegative, ReplaceKansaiNegative);
    if (dialectForms && (Contains(text, "と") || Contains(text, "ど")))
        text = ReplaceAll(text, ContractedToru, ReplaceContractedToru);
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
    if (exclamationNa && Contains(text, "な")) text = ReplaceAll(text, ExclamationNa, ReplaceExclamationNa);
    return ReplaceAll(text, Dekinu, [](const Match&) { return std::string("できない"); });
}

}  // namespace hdTrans::pipeline::internal
