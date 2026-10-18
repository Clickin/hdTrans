// Grammar passes of the text path: the readings of a verb's られ, and the conjugating
// targets of passive verbs that have no static family.
#include <string>
#include <string_view>

#include "hdTrans/output/korean.h"
#include "hdTrans/output/families.h"
#include "hdTrans/text/utf8.h"
#include "hdTrans/word/flush.h"
#include "hdTrans/word/strings.h"
#include "hdTrans/word/util.h"

namespace hdTrans::word {
namespace {

// Passive verbs written with a Korean passive rather than 지다, by the surface of the item.
// A class after the colon is the conjugation class of the Korean lemma.
struct PassiveLemma {
    std::string_view surface;
    std::string_view lemma;
};

constexpr PassiveLemma kPassiveLemmas[] = {
    {"恐れ", "두려움을 사다"}, {"褒め", "칭찬받다"}, {"助け", "도움을 받다"}, {"責め", "비난받다"}, {"認め", "인정받다"},
    {"求め", "요구되다"}, {"見つめ", "응시당하다"}, {"遠ざけ", "경원시되다"}, {"食べ", "먹히다"}, {"忘れ", "잊히다"},
    {"捕まえ", "붙잡히다"}, {"閉じ込め", "갇히다"}, {"見捨て", "버림받다"}, {"抱きしめ", "안기다"}, {"迎え", "환영받다"},
    {"苛め", "괴롭힘을 당하다"}, {"虐げ", "학대받다"}, {"慰め", "위로받다"}, {"称え", "칭송받다"}, {"讃え", "칭송받다"},
    {"崇め", "숭배받다"}, {"咎め", "문책당하다"}, {"舐め", "얕보이다:규칙"}, {"見下げ", "멸시당하다"}, {"教え", "가르침을 받다"},
    {"授け", "받다"}, {"告げ", "듣다:ㄷ"}, {"止め", "저지당하다"}, {"見せ", "보여지다"},
    {"押し付け", "떠맡겨지다"}, {"責め立て", "추궁당하다"}, {"追い詰め", "몰리다"}, {"攻め", "공격받다"},
    {"何を言", "무슨 말을 듣다:ㄷ"}, {"追い出", "쫓겨나다"}, {"叩き出", "쫓겨나다"}, {"脅", "협박당하다"}, {"乱入", "난입당하다"},
    {"閉め", "닫히다:규칙"}, {"開け", "열리다:규칙"}, {"連れ", "이끌리다:규칙"}, {"込め", "담기다:규칙"},
    {"括り付け", "묶이다:규칙"}, {"縛り付け", "묶이다:규칙"}, {"入れ替え", "바뀌다:규칙"}, {"取り替え", "바뀌다:규칙"},
    {"捨て", "버려지다:규칙"}, {"育て", "길러지다:규칙"},
};

// The target of the passive lemma of surface; false when surface has none.
bool PassiveTarget(Flusher& f, std::string_view surface, std::string& target) {
    for (const PassiveLemma& p : kPassiveLemmas) {
        if (p.surface != surface) continue;
        const std::string_view lemma = p.lemma;
        const std::size_t colon = lemma.find(':');
        const std::string_view ko = lemma.substr(0, colon);
        const std::string_view cls = colon == std::string_view::npos ? std::string_view() : lemma.substr(colon + 1);
        output::Conjugated c = output::Conjugating(ko, '&', cls, f.families);
        if (!c.error.empty()) return false;
        target = std::move(c.target);
        return true;
    }
    return false;
}

// Verbs whose られ is read as the passive whatever their objects (perception and attitude).
constexpr std::string_view kPassiveVerbs[] = {
    "恐れ", "求め", "向け", "認め", "褒め", "責め", "見つめ", "称え", "讃え", "崇め", "見捨て", "名付け", "閉じ込め",
    "捕らえ", "授け", "咎め", "苛め", "虐げ", "嫌がらせ", "撫で", "睨め", "抱え", "付け", "抱き上げ",
};

// Ichidan stems whose affirmative られた is mostly the potential.
constexpr std::string_view kAbilityVerbs[] = {
    "見つけ", "答え", "覚え", "信じ", "食べ", "耐え", "考え", "止め", "抑え", "堪え", "決め", "変え", "見分け", "避け",
    "続け", "受け止め", "乗り越え", "生き延び", "逃げ", "着",
};

// Whether stem is a compound verb of action on someone (睨み付け, 突きつけ, 投げかけ, 責め立て).
bool CompoundAction(std::string_view stem) {
    constexpr std::string_view suffixes[] = {"付け", "つけ", "かけ", "掛け", "立て", "かかえ", "抱え"};
    for (const std::string_view s : suffixes) {
        if (stem.ends_with(s) && stem.size() > s.size()) return true;
    }
    return false;
}

// A verb item with a record that takes an object (part code class 3 to 6).
bool Transitive(const Item* it) {
    const int count = static_cast<int>(it->group->entries.size());
    for (int slot = 0; slot < count; slot++) {
        const char c = At(it->Code(slot), 1);
        if (c >= '3' && c <= '6') return true;
    }
    return false;
}

// The passive reading of a られた or られて with no other cue.
bool NarrativePassive(const Item* it, std::string_view rest) {
    if (!rest.starts_with("た") && !rest.starts_with("て")) return false;
    return !InSet(kAbilityVerbs, it->group->surface);
}

// The passive reading of the られ of verb item it; rest is the text after られ:
//   - a negative, conditional or volitional form and the polite non-past are potential;
//   - a verb that takes no object is potential, unless it is listed or a compound action;
//   - ている, ていた, てる, ており are passive;
//   - going back through the clause, the nearest を (potential unless listed) or an agent
//     に, から, によって, により after a thing-free word decides;
//   - listed verbs and narrative forms are passive;
//   - a subject は or が that is a thing makes the reading passive, unless the verb is the
//     plain non-past form;
//   - otherwise potential.
bool PassiveReading(const Item* it, std::string_view rest) {
    constexpr std::string_view kPotentialForms[] = {"な", "ず", "ぬ", "ん", "ませ", "ます", "れば", "よう", "たら", "たり"};
    for (const std::string_view p : kPotentialForms) {
        if (rest.starts_with(p)) return false;
    }
    const bool listed = InSet(kPassiveVerbs, it->group->surface) || CompoundAction(it->group->surface);
    if (!listed && !Transitive(it)) return false;
    constexpr std::string_view kPassiveForms[] = {"てい", "てる", "てお", "てた"};
    for (const std::string_view p : kPassiveForms) {
        if (rest.starts_with(p)) return true;
    }
    const Item* subject = nullptr;
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        const std::string_view s = p->group->surface;
        if (s == "を" && !listed) {
            return false;
        } else if (s == "に" || s == "から" || s == "によって" || s == "により") {
            const Item* a = p->prev;
            if (a != nullptr && !Thing(a->Attr(std::max(a->selected, 0)))) return true;
        } else if ((s == "は" || s == "が") && p->prev != nullptr && subject == nullptr) {
            subject = p->prev;
        } else if (p->detail->Yougo() || s.empty() || Has("、。,「」『』！？", s)) {
            break;
        }
    }
    if (listed || NarrativePassive(it, rest)) return true;
    return subject != nullptr && Thing(subject->Attr(std::max(subject->selected, 0))) && !rest.starts_with("る");
}

// Writes a passive verb item of kPassiveLemmas as its Korean passive in the active voice.
void WriteLexicalPassive(Flusher& f, Item* it) {
    std::string lexical;
    if (!PassiveTarget(f, it->group->surface, lexical)) return;
    it->detail->record = 0;
    it->lexical = true;
    for (Entry* e : it->group->entries) e->dst = lexical;
}

// The subject form of 나, 저 and 너 (before 가), or "" for other lemmas.
std::string_view SubjectPronoun(std::string_view lemma) {
    if (lemma == "나") return "내";
    if (lemma == "저") return "제";
    if (lemma == "너") return "네";
    return {};
}

// Whether the text after ではな is a contrast that goes on (大した傷ではないが), which keeps the
// topic particle.
bool Contrast(std::string_view rest) {
    constexpr std::string_view kForms[] = {"い", "かった"};
    constexpr std::string_view kParticles[] = {"が", "けど", "けれど", "ですが", "ですけど", "のですが", "のだが",
                                               "んだが", "んですが", "んだけど", "でしょうが", "だろうが"};
    for (const std::string_view form : kForms) {
        if (!rest.starts_with(form)) continue;
        std::string_view after = rest.substr(form.size());
        if (after.starts_with(kJaComma)) after.remove_prefix(kJaComma.size());
        for (const std::string_view p : kParticles) {
            if (after.starts_with(p)) return true;
        }
    }
    return false;
}

// Nouns that name a quality: the の before an adjective after them is the subject.
constexpr std::string_view kAttributeNouns[] = {
    "香り", "色", "頭", "仲", "気持ち", "運", "口", "腕", "背", "人気", "面倒見", "品", "質", "勘", "性格", "器量",
    "愛想", "要領", "手際", "物覚え", "見晴らし", "日当たり", "風通し", "味", "気前", "気立て", "育ち", "顔色",
    "顔立ち", "目つき", "目付き", "人当たり", "評判", "趣味", "体格", "態度", "機嫌", "具合", "調子",
};

// Stems before い of the predicates of existence, quantity or knowing.
constexpr std::string_view kSubjectPredicates[] = {
    "居な", "いな", "少な", "無", "知らな", "分からな", "わからな", "見えな", "聞こえな", "多", "少", "絶えな",
};

// Whether text starts like a negative ending: な, ね (ねえ), ませ, ず, ぬ, ん.
bool NegativeStart(std::string_view text) {
    constexpr std::string_view kStarts[] = {kJaNa, kJaNe, kJaMase, kJaZu, kJaNu, kJaN};
    for (const std::string_view p : kStarts) {
        if (text.starts_with(p)) return true;
    }
    return false;
}

}  // namespace

// Dictionary attributes of a thing: some, none of them a person, an animal or a god.
std::string ItemLemma(Flusher& f, const Item* it, int slot) {
    return output::Lemma(it->EntryText(slot), f.families);
}

bool Thing(std::string_view attr) {
    if (attr.empty()) return false;
    constexpr std::string_view kPersons = "VHn VQw VQm VGn VAm QPN QNP";
    for (std::size_t i = 0; i + 3 <= attr.size(); i += 3) {
        const std::string_view c = attr.substr(i, 3);
        if (Has(kPersons, c) || (c >= "QAA" && c <= "QAH")) return false;
    }
    return true;
}

bool Person(std::string_view attr) { return !attr.empty() && !Thing(attr); }

bool SelectLemma(Flusher& f, Item* it, std::string_view lemma) {
    const int count = static_cast<int>(it->group->entries.size());
    for (int slot = 0; slot < count; slot++) {
        if (output::Lemma(it->EntryText(slot), f.families) == lemma) {
            it->selected = slot;
            return true;
        }
    }
    return false;
}

bool SelectLemmaPrefix(Flusher& f, Item* it, std::string_view prefix) {
    const int count = static_cast<int>(it->group->entries.size());
    for (int slot = 0; slot < count; slot++) {
        if (output::Lemma(it->EntryText(slot), f.families).starts_with(prefix)) {
            it->selected = slot;
            return true;
        }
    }
    return false;
}

void PassiveRecords(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->group->entries.empty()) continue;
        if (it->detail->record == 1) {
            WriteLexicalPassive(f, it);
            continue;
        }
        if (it->detail->record != 4) continue;
        const char k = At(it->Code(0), 2);
        if (k != '7' && k != ';' && k != '=') continue;
        std::string_view rest = From(it->text, it->start + static_cast<long>(it->group->surface.size()));
        if (!rest.starts_with("られ")) continue;
        rest.remove_prefix(std::string_view("られ").size());
        if (!PassiveReading(it, rest)) continue;
        it->detail->record = 1;
        for (Entry* e : it->group->entries) {
            if (e->gattr.ends_with("Ka")) e->gattr = e->gattr.substr(0, e->gattr.size() - 2) + "Pi";
        }
        WriteLexicalPassive(f, it);
    }
}


void GoNotSay(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* p = it->prev;
        if (p == nullptr || p->prev == nullptr || it->group->surface != "い" || !it->detail->Yougo() ||
            !output::Lemma(it->EntryText(it->selected), f.families).starts_with("말하") ||
            !Has(" に へ まで から ", " " + p->group->surface + " ") || Person(p->prev->Attr(p->prev->selected))) {
            continue;
        }
        if (SelectLemma(f, it, "가다")) continue;
        output::Conjugated c = output::Conjugating("가다", '&', "", f.families);
        if (Entry* e = it->group->At(it->selected)) e->dst = c.target;
    }
}

void PositiveIke(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (it->group->surface != kJaIke || !it->EntryText(it->selected).starts_with(kKoAn)) continue;
        const std::string_view direct = it->DirectSurface();
        if (NegativeStart(direct) || (direct.empty() && it->next != nullptr && NegativeStart(it->next->group->surface))) {
            continue;
        }
        const int count = static_cast<int>(it->group->entries.size());
        for (int slot = 0; slot < count; slot++) {
            if (it->EntryText(slot).starts_with(kKoGalSu)) {
                it->selected = slot;
                break;
            }
        }
    }
}

void CopulaDeAru(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const Item* p = it->prev;
        if (p != nullptr && it->group->surface == "であ" && !Has("と ここ そこ あそこ どこ", p->group->surface) &&
            output::Lemma(it->EntryText(it->selected), f.families).starts_with("에서 만나")) {
            SelectLemma(f, it, "이다");
        }
    }
}

void NegativeCopula(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* p = it->prev;
        if (p == nullptr) continue;
        const Entry* pe = p->group->At(p->selected);
        if (p->detail->cls != kDetailInfo || pe == nullptr || pe->cls != 'N') continue;
        const std::string plemma = output::Lemma(p->EntryText(p->selected), f.families);
        if (plemma.ends_with("히") || TrimSpace(plemma) == ",") continue;
        if (it->group->surface != "ではな" || Contrast(From(it->text, it->start + static_cast<long>(it->group->surface.size())))) {
            continue;
        }
        if (!output::Lemma(it->EntryText(it->selected), f.families).starts_with("(은)는 아니") ||
            !SelectLemmaPrefix(f, it, "(이)가 아니")) {
            continue;
        }
        if (const std::string_view subject = SubjectPronoun(plemma); !subject.empty()) SelectLemma(f, p, subject);
    }
}

void SubjectBeforeGa(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* p = it->prev;
        if (p == nullptr || it->selected < 0 || p->selected < 0 ||
            output::Lemma(it->EntryText(it->selected), f.families) != "(이)가") {
            continue;
        }
        if (it->group->surface == "が") {
            const std::string_view subject = SubjectPronoun(output::Lemma(p->EntryText(p->selected), f.families));
            if (!subject.empty() && !p->detail->Yougo()) {
                if (Entry* e = p->group->At(p->selected)) e->dst = std::string(subject);
            }
        } else if (it->group->surface == "の") {
            const Item* n = it->next;
            if (n != nullptr && n->detail->Yougo() && Has(n->group->surface, "を")) SelectLemma(f, it, "의");
        }
    }
}

void SubjectNo(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* p = it->prev;
        Item* n = it->next;
        if (p == nullptr || n == nullptr || n->next == nullptr || it->group->surface != "の" || it->selected < 0 ||
            output::Lemma(it->EntryText(it->selected), f.families) != "의" || p->selected < 0 ||
            At(p->Code(p->selected), 0) != '1' || n->selected < 0 ||
            (!InSet(kSubjectPredicates, n->group->surface) &&
             !(InSet(kAttributeNouns, p->group->surface) && At(n->Code(n->selected), 0) == '7')) ||
            n->next->selected < 0 || At(n->next->Code(n->next->selected), 0) != '1') {
            continue;
        }
        if (SelectLemma(f, it, "(이)가")) {
            const std::string_view subject = SubjectPronoun(output::Lemma(p->EntryText(p->selected), f.families));
            if (!subject.empty()) {
                if (Entry* e = p->group->At(p->selected)) e->dst = std::string(subject);
            }
        }
    }
}

namespace {

// Stems of the verbs of resultState whose ている is a state, read without an object.
constexpr std::string_view kResultVerbs[] = {
    "濡れ", "座", "立", "残", "入", "生き", "落ち", "似", "疲れ", "並", "変わ", "繋が", "離れ", "揃", "空", "終わ", "整",
    "載", "止ま", "倒れ", "集ま", "壊れ", "閉ま", "浮か", "隠れ", "広が", "固ま", "戻", "浮", "減", "溜ま", "潜", "掛か",
    "混じ", "死", "太", "痩せ", "決ま", "開", "閉じ", "腫れ", "枯れ", "割れ", "汚れ", "曲が", "折れ", "破れ", "崩れ", "乾",
    "冷え", "腐", "詰ま", "埋ま", "刺さ", "慣れ", "晴れ", "曇", "凍", "外れ", "取れ", "引きこも", "引き籠も", "惚れ", "ハマ",
    "はま", "嵌ま", "突っ伏", "埋もれ", "腰を下ろ", "落ち着", "腰かけ", "腰掛け", "済", "な", "埋め尽くされ", "覚め",
};

// Verbal nouns whose されている is a state.
constexpr std::string_view kResultNouns[] = {
    "舗装", "設置", "固定", "封印", "整備", "保存", "記載", "記録", "配置", "装備", "収納", "展示", "包装", "印刷", "登録",
    "接続", "密閉", "施錠", "武装", "装飾", "彫刻", "刻印", "整頓", "固定化",
};

// Stems of the passive verbs whose ている is a state.
constexpr std::string_view kResultPassives[] = {
    "並べ", "置", "囲", "覆", "積", "飾", "掛け", "盛", "刻", "書", "貼", "吊", "敷", "張", "描", "記", "包", "詰め",
    "閉じ込め", "埋め", "刺", "縛", "隠", "残", "向け", "付け", "込め", "組", "建て", "植え", "収め", "閉ざ", "塗", "彫",
    "繋", "結", "束ね", "挟", "重ね", "据え", "閉め", "開け", "括り付け", "縛り付け",
};

// The adverbs of gradual change before a ている of なる (a progression).
constexpr std::string_view kGradualAdverbs[] = {
    "次第", "徐々", "どんどん", "だんだん", "段々", "ますます", "益々", "日に日に", "少しずつ", "みるみる",
};

// Stems of the verbs of noticing or remembering that take 못하다 (notYet).
constexpr std::string_view kNotYetVerbs[] = {"気づ", "気付", "気がつ", "気が付", "覚え"};

// Whether an adverb of gradual change stands before it in its sentence.
bool GradualBefore(const Item* it) {
    for (const Item* p = it->prev; p != nullptr; p = p->prev) {
        for (const std::string_view a : kGradualAdverbs) {
            if (p->group->surface.starts_with(a)) return true;
        }
    }
    return false;
}

// The particles and punctuation of seeAnd (before the 見る て they do not change).
constexpr std::string_view kSeeAndFollowers = " は も を が の に と で や か よ ね な ";

}  // namespace

void ResultState(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0) continue;
        const std::string_view stem = it->group->surface;
        const char c1 = At(it->Code(it->selected), 1);
        const bool object = c1 >= '3' && c1 <= '6';
        if (stem == "な" && GradualBefore(it)) continue;
        const bool nounPassive = stem == "され" && it->prev != nullptr && InSet(kResultNouns, it->prev->group->surface);
        const bool state = (it->detail->record == 0 && !it->lexical && (InSet(kResultVerbs, stem) || nounPassive) && !object) ||
                           ((it->detail->record == 1 || it->lexical) && InSet(kResultPassives, stem));
        if (!state) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        const std::string lemma = output::Lemma(e->dst, f.families);
        if (!lemma.starts_with("1고")) continue;
        const std::string_view rest = std::string_view(lemma).substr(std::string_view("1고").size());
        if (rest.starts_with(" 있") || (rest.empty() && it->next != nullptr && ItemLemma(f, it->next, it->next->selected).starts_with("있"))) {
            e->dst = "6" + std::string(rest);
        }
    }
}

void NotYet(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        Item* n = it->next;
        if (n == nullptr || !it->detail->Yougo() || it->DirectIndex() < 0) continue;
        const Entry* direct = it->DirectEntry(it->DirectIndex());
        if (direct == nullptr || direct->dst != std::string("1") + std::string(kKoJi) || n->group->surface != "いな" ||
            ItemLemma(f, n, n->selected) != "않다" || !std::any_of(std::begin(kNotYetVerbs), std::end(kNotYetVerbs),
                                                                    [&](std::string_view v) { return it->group->surface.ends_with(v); })) {
            continue;
        }
        output::Conjugated target = output::Conjugating("못하다", '$', "", f.families);
        if (Entry* e = n->group->At(n->selected)) e->dst = target.target;
        Entry* d = n->DirectEntry(n->DirectIndex());
        if (d == nullptr) continue;
        for (const std::string_view form : {std::string_view("1는다"), std::string_view("1다")}) {
            if (d->dst.starts_with(form)) {
                d->dst = "4다" + d->dst.substr(form.size());
                break;
            }
        }
    }
}

void SeeAnd(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0 || ItemLemma(f, it, it->selected) != "보다" ||
            !it->group->surface.ends_with("見") || it->DirectSurface() != "て") {
            continue;
        }
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr || e->dst != "6" || it->prev == nullptr || it->prev->group->surface != "を") continue;
        if (UtteranceEnd(f.w, it) && !From(it->text, it->start + it->length).starts_with(kJaComma)) continue;
        if (const Item* n = it->next; n != nullptr) {
            const std::string_view next = n->group->surface;
            bool stop = next.starts_with("」") || next.starts_with("』");
            for (const std::string_view a : kTeAuxiliaries) stop = stop || next.starts_with(a);
            if (stop || Has(kSeeAndFollowers, " " + std::string(next) + " ")) continue;
        }
        e->dst = "1고";
    }
}

}  // namespace hdTrans::word
