// The constraint rules and the pass that applies them to the candidates of a lookup.
#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/text/hangul.h"
#include "core/text/kana.h"
#include "core/text/utf8.h"
#include "core/word/constraint_rules.h"
#include "core/word/worker.h"

namespace hdTrans::word {

namespace {

constexpr ConstraintRule kRules[] = {
    {"honorific-katakana", ConstraintWord::Katakana, "", ConstraintContext::Honorific, ConstraintReject::NonPerson,
     "가타카나 단어 + 경칭(さん·くん·様 등): 사람·동물·직함 뜻이 없으면 일반 뜻을 버리고 인명 읽기 (ハル君 헐군 → 하루군)"},
    {"honorific-kanji-name", ConstraintWord::Kanji, "", ConstraintContext::FamiliarHonorific,
     ConstraintReject::NonName,
     "한자 단어 + くん·君·ちゃん·たん: 사전에 인명 뜻이 있고 사람 명사 뜻이 없으면 인명 뜻만 (翼くん 날개 군 → 츠바사군)"},
    {"honorific-kanji-name-place", ConstraintWord::Kanji, "", ConstraintContext::Honorific,
     ConstraintReject::NonNameBesidePlace,
     "한자 단어 + 경칭: 인명 뜻 말고는 지명 뜻뿐이면 인명 뜻만 (太郎殿 타로전 → 타로님)"},
    {"kimi-after-katakana", ConstraintWord::Source, "君", ConstraintContext::AfterKatakana, ConstraintReject::Pronoun,
     "가타카나 바로 뒤의 君은 대명사(너·네)가 아니라 접미사 군 (バカ君が 바보네가 → 바보군이)"},
    {"quote-after-exclamation", ConstraintWord::Source, "と", ConstraintContext::AfterExclamation,
     ConstraintReject::NonQuote, "！·？ 바로 뒤의 と는 인용 조사 라고 (ドン！と 돈! 와 → 돈! 라고)"},
    {"quote-tte-after-pause", ConstraintWord::Source, "って", ConstraintContext::AfterPause,
     ConstraintReject::NonQuote, "！·？·、·… 뒤에서 명사(奴·こと·話 등)를 꾸미는 って는 인용 (休暇、って奴 휴가, 는 녀석 → 휴가, 라는 녀석)"},
    {"iwaku-line-opening", ConstraintWord::Source, "曰く", ConstraintContext::LineOpening, ConstraintReject::NonAdverb,
     "줄 첫머리의 曰く、는 전하는 말을 여는 부사 (曰く、彼は 왈, 그는 → 듣자 하니, 그는)"},
    {"iwaku-elsewhere", ConstraintWord::Source, "曰く", ConstraintContext::Elsewhere, ConstraintReject::Adverb,
     "줄 첫머리가 아닌 曰く는 부사 뜻을 버림 (先生曰く 선생님 왈)"},
};

struct HonorificSuffix {
    std::string_view text;
    bool familiar;
};

constexpr HonorificSuffix kHonorifics[] = {
    {"さん", false}, {"さま", false}, {"様", false}, {"殿", false}, {"氏", false},  {"先輩", false},
    {"先生", false}, {"くん", true},  {"君", true},  {"ちゃん", true}, {"たん", true},
};

constexpr std::string_view kOpeningBrackets[] = {"「", "『", "（", "(", "“", "‘", "【", "〈", "《", "［", "["};

// What continues a word rather than follows a particle (とって, ととと, とぉぉ).
constexpr std::string_view kDrawls[] = {"っ", "と", "ぉ", "ぁ", "ー", "～", "↓", "↑"};
// The nouns that a quotation in って modifies (って奴 라는 녀석).
constexpr std::string_view kQuotedNouns[] = {"奴", "やつ", "こと", "事", "もの", "物", "感じ", "話", "言葉", "のは", "のが"};

bool AllOfClass(std::string_view s, int cls) {
    if (s.empty()) return false;
    for (std::string_view t = s; !t.empty();) {
        const std::string_view c = FirstChar(t);
        if (c.empty() || NameClassOf(c) != cls) return false;
        t.remove_prefix(c.size());
    }
    return true;
}

bool HangulText(std::string_view s) {
    if (s.empty()) return false;
    while (!s.empty()) {
        const text::Rune r = text::DecodeRune(s);
        if (r.cp < U'가' || r.cp > U'힣') return false;
        s.remove_prefix(r.size);
    }
    return true;
}

bool HonorificAt(std::string_view s, bool familiarOnly) {
    for (const HonorificSuffix& h : kHonorifics) {
        if ((h.familiar || !familiarOnly) && s.starts_with(h.text)) return true;
    }
    return false;
}

bool NameSense(const dict::Codes& c) {
    return c.sem.HasPrefix("QPN") || c.gram.HasPrefix("Wa") || c.gram.HasPrefix("Wb");
}

// A sense of a person, a group of people, an animal or a role.
bool PersonSense(const dict::Codes& c) {
    for (const std::string_view code : {"QPN", "QNP", "VHn", "VQw", "VQm", "VGn", "VGt", "VAm", "UAq", "UQf"}) {
        if (c.sem.HasCode(code)) return true;
    }
    return c.sem.HasCodeIn("QAA", "QAH") || NameSense(c);
}

bool PersonNoun(const dict::Codes& c) {
    for (const std::string_view code : {"VHn", "VQw", "VQm", "VGn", "UAq", "UQf"}) {
        if (c.sem.HasCode(code)) return true;
    }
    return false;
}

// Whether the word source, after the character before, is the kind of word rule is for.
bool WordFits(const ConstraintRule& rule, std::string_view source, std::string_view before) {
    switch (rule.word) {
    case ConstraintWord::Katakana:
        return text::RuneCount(source) >= 2 && AllOfClass(source, kClassKatakana) &&
               NameClassOf(before) != kClassKatakana;
    case ConstraintWord::Kanji:
        return AllOfClass(source, kClassKanji) && NameClassOf(before) != kClassKanji && FindHonorificPrefix(before) == -1;
    case ConstraintWord::Source:
        return source == rule.source;
    }
    return false;
}

bool StartsWithAny(std::string_view s, std::span<const std::string_view> list) {
    return std::any_of(list.begin(), list.end(), [s](std::string_view p) { return s.starts_with(p); });
}

// Whether the context of rule holds for the word source of the candidates list: after is
// the text after it, before the character before it in its token, previous the character
// before it past spaces and token ends.
bool ContextFits(const ConstraintRule& rule, const std::vector<dict::Candidate>& list, std::string_view source,
                 std::string_view after, std::string_view before, std::string_view previous) {
    const bool exclamation = previous == "！" || previous == "？" || previous == "!" || previous == "?";
    switch (rule.context) {
    case ConstraintContext::Honorific:
    case ConstraintContext::FamiliarHonorific:
        return HonorificAt(after, rule.context == ConstraintContext::FamiliarHonorific) &&
               std::none_of(list.begin(), list.end(), [&](const dict::Candidate& c) { return c.source.size() > source.size(); });
    case ConstraintContext::AfterKatakana:
        return NameClassOf(before) == kClassKatakana;
    case ConstraintContext::AfterExclamation:
        return exclamation && !StartsWithAny(after, kDrawls);
    case ConstraintContext::AfterPause:
        return (exclamation || previous == "、" || previous == "…" || previous == "‥" || previous == ",") &&
               StartsWithAny(after, kQuotedNouns);
    case ConstraintContext::LineOpening:
    case ConstraintContext::Elsewhere:
        return ((previous.empty() || StartsWithAny(previous, kOpeningBrackets)) &&
                (after.starts_with("、") || after.starts_with("，") || after.starts_with(","))) ==
               (rule.context == ConstraintContext::LineOpening);
    }
    return false;
}

// The person reading of the name layer for the katakana word source that replaces its
// senses same, or null when one of them is a person or sounds like the kana.
const dict::Record* PersonReading(const dict::Snapshot& snapshot, const std::vector<dict::Candidate>& list,
                                  const std::vector<std::size_t>& same, std::string_view source,
                                  std::vector<dict::Record>& records) {
    if (std::any_of(same.begin(), same.end(),
                    [&](std::size_t k) { return PersonSense(*list[k].codes) || list[k].user != nullptr; })) {
        return nullptr;
    }
    const std::string reading = text::FromKana(source, text::Preset::Fan);
    if (reading.empty() ||
        std::any_of(same.begin(), same.end(), [&](std::size_t k) { return text::SameSound(list[k].target, reading); })) {
        return nullptr;
    }
    records = snapshot.NameRecords(source);
    for (const dict::Record& r : records) {
        if (r.codes != nullptr && r.codes->sem.HasPrefix("QPN") && text::SameSound(r.target, reading)) return &r;
    }
    return nullptr;
}

// The name sense among same that replaces the others, or same.end() when there is none or
// a person noun, a verb or adjective as long as source, or (beside places) another sense
// than a place is among the candidates.
std::vector<std::size_t>::const_iterator NameAmong(const ConstraintRule& rule, const std::vector<dict::Candidate>& list,
                                                    const std::vector<std::size_t>& same, std::string_view source) {
    auto codes = [&](std::size_t k) -> const dict::Codes& { return *list[k].codes; };
    const auto name = std::find_if(same.begin(), same.end(), [&](std::size_t k) {
        return NameSense(codes(k)) && list[k].user == nullptr && HangulText(list[k].target);
    });
    const bool blocked =
        std::any_of(list.begin(), list.end(), [&](const dict::Candidate& c) {
            return c.codes != nullptr && AnyOf("478", c.codes->part.ClassDigit()) && c.source.size() >= source.size();
        }) ||
        std::any_of(same.begin(), same.end(), [&](std::size_t k) { return PersonNoun(codes(k)) || list[k].user != nullptr; }) ||
        std::all_of(same.begin(), same.end(), [&](std::size_t k) { return NameSense(codes(k)); }) ||
        (rule.reject == ConstraintReject::NonNameBesidePlace && !std::all_of(same.begin(), same.end(), [&](std::size_t k) {
             return NameSense(codes(k)) || codes(k).sem.HasCode("QCT") || codes(k).sem.HasCode("QNN");
         }));
    return blocked ? same.end() : name;
}

}  // namespace

std::span<const ConstraintRule> ConstraintRules() { return kRules; }

// Text path, mode 0. For each rule and each source of the candidates that the rule's word
// and context fit, the rejected candidates leave the list; a name made from a person
// reading takes the place of the first of them.
void Worker::ApplyConstraints(dict::Candidates& found, std::string_view query) {
    const std::string_view surface = transformSurface;
    if (!surface.ends_with(query)) return;
    const std::string_view head = surface.substr(0, surface.size() - query.size());
    const std::string_view before = LastChar(head);
    std::string_view previous = LastChar(TrimRunes(head, " 　"));
    for (int j = current - 1; previous.empty() && j >= 0; j--) {
        const std::string_view t = TrimRunes(Tok(j).text, " 　");
        if (!t.empty()) previous = LastChar(t);
        else if (Tok(j).text.empty()) break;
    }
    if (previous.empty()) previous = settings.before;
    std::vector<dict::Candidate>& list = found.list;
    for (const ConstraintRule& rule : kRules) {
        std::size_t i = 0;
        while (i < list.size()) {
            const std::string_view source = list[i].source;
            const bool first = std::none_of(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(i),
                                            [&](const dict::Candidate& c) { return c.source == source; });
            if (source.empty() || list[i].codes == nullptr || !first || !WordFits(rule, source, before) ||
                !ContextFits(rule, list, source, From(query, static_cast<long>(source.size())), before, previous)) {
                i++;
                continue;
            }
            std::vector<std::size_t> same;
            for (std::size_t k = i; k < list.size(); k++) {
                if (list[k].source == source && list[k].codes != nullptr) same.push_back(k);
            }
            std::vector<std::size_t> rejected;
            const dict::Codes* nameCodes = nullptr;
            std::string_view nameTarget;
            int nameId = 0;
            std::vector<dict::Record> records;
            switch (rule.reject) {
            case ConstraintReject::NonPerson:
                if (const dict::Record* r = PersonReading(snapshot, list, same, source, records)) {
                    nameCodes = r->codes->asPlain, nameTarget = r->target, nameId = r->id;
                    rejected = same;
                }
                break;
            case ConstraintReject::NonName:
            case ConstraintReject::NonNameBesidePlace:
                if (const auto name = NameAmong(rule, list, same, source); name != same.end()) {
                    nameCodes = list[*name].codes->asPlain, nameTarget = list[*name].target, nameId = list[*name].id;
                    rejected = same;
                }
                break;
            case ConstraintReject::Pronoun:
                for (const std::size_t k : same) {
                    const dict::PartCode p = list[k].codes->part;
                    if (p.Tag() == 'N' && p.ClassDigit() == '1' && p.Sub() == '2') rejected.push_back(k);
                }
                if (rejected.size() == same.size()) rejected.clear();
                break;
            case ConstraintReject::NonAdverb:
                for (const std::size_t k : same) {
                    if (list[k].codes->part.Tag() != 'E') rejected.push_back(k);
                }
                if (rejected.size() == same.size()) rejected.clear();
                break;
            case ConstraintReject::Adverb:
                for (const std::size_t k : same) {
                    if (list[k].codes->part.Tag() == 'E') rejected.push_back(k);
                }
                if (rejected.size() == same.size()) rejected.clear();
                break;
            case ConstraintReject::NonQuote:
                for (const std::size_t k : same) {
                    const std::string_view t = list[k].target;
                    if (!Has(t, "라고") && !Has(t, "다고") && !Has(t, "라는") && !Has(t, "다는")) rejected.push_back(k);
                }
                if (rejected.size() == same.size()) rejected.clear();
                break;
            }
            if (rejected.empty()) {
                i++;
                continue;
            }
            for (auto k = rejected.rbegin(); k != rejected.rend(); ++k) {
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(*k));
            }
            found.count -= static_cast<int>(rejected.size());
            if (nameCodes == nullptr) continue;  // list[i] is the next source now
            if (arena.namesUsed == arena.names.size()) arena.names.emplace_back();
            std::vector<dict::UserEntry>& kept = arena.names[arena.namesUsed++];
            kept.clear();
            dict::UserEntry& e = kept.emplace_back();
            e.key.assign(source);
            e.target.assign(nameTarget);
            e.codes = nameCodes;
            e.file = -2;
            e.line = nameId;
            e.name = true;
            e.nameLayer = true;
            dict::Candidate name;
            name.source = e.key;
            name.target = e.target;
            name.codes = e.codes;
            name.user = &e;
            list.insert(list.begin() + static_cast<std::ptrdiff_t>(rejected.front()), name);
            found.count++;
            i++;
        }
    }
}

}  // namespace hdTrans::word
