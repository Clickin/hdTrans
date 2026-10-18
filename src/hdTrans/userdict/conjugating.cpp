#include "hdTrans/userdict/conjugating.h"

#include <algorithm>
#include <map>
#include <span>
#include <vector>

#include "hdTrans/output/markers.h"
#include "hdTrans/text/hangul.h"
#include "hdTrans/userdict/compile.h"

namespace hdTrans::userdict {

namespace {

using internal::Runes;
using internal::Utf8;

constexpr int kVowelA = 0, kVowelAe = 1, kVowelYa = 2, kVowelYae = 3, kVowelEo = 4, kVowelE = 5, kVowelYeo = 6,
              kVowelYe = 7, kVowelO = 8, kVowelWa = 9, kVowelWae = 10, kVowelOe = 11, kVowelU = 13, kVowelWo = 14,
              kVowelEu = 18, kVowelI = 20;
constexpr int kFinalN = 4, kFinalD = 7, kFinalL = 8, kFinalLM = 10, kFinalM = 16, kFinalB = 17, kFinalS = 19,
              kFinalSS = 20, kFinalH = 27;
constexpr int kInitialR = 5, kInitialNone = 11;

struct Jamo {
    int initial = 0, vowel = 0, final = 0;
    bool ok = false;
};

Jamo Decompose(char32_t r) {
    const auto s = text::Split(r);
    if (!s) return {};
    return {s->initial, s->vowel, s->final, true};
}

std::string Compose(int initial, int vowel, int final) {
    return Utf8(std::u32string(1, text::Join(initial, vowel, final)));
}

std::string Str(char32_t r) { return Utf8(std::u32string(1, r)); }

bool Bright(int vowel) { return vowel == kVowelA || vowel == kVowelYa || vowel == kVowelO; }

int FormIndex(char code) {
    const std::size_t i = kFormCodes.find(code);
    return i == std::string_view::npos ? -1 : static_cast<int>(i);
}

int MarkerIndex(char marker) { return marker == '&' ? 0 : marker == '$' ? 1 : marker == '%' ? 2 : -1; }

std::span<const output::internal::MarkerRow> MarkerTable(char marker) {
    switch (marker) {
        case '&':
            return output::internal::SourceMarkerTable();
        case '$':
            return output::internal::DollarMarkerTable();
        case '%':
            return output::internal::PercentMarkerTable();
    }
    return {};
}

// A static family: its id ("&A1@") and rows.
struct Family {
    std::string id;
    FamilyRows rows;
};

// The static families of each marker by their row 1, in id order.
using FamilyIndex = std::map<std::string, std::vector<Family>, std::less<>>;

const FamilyIndex& StaticFamilies(char marker) {
    static const std::array<FamilyIndex, 3> index = [] {
        std::array<FamilyIndex, 3> out;
        for (const char m : {'&', '$', '%'}) {
            std::vector<Family> list;
            for (const output::internal::MarkerRow& row : MarkerTable(m)) {
                const std::string_view key = row.key;
                const auto graphic = [](char c) { return c >= 0x21 && c <= 0x7E; };
                if (key.size() != 5 || key[3] != '@' || !graphic(key[1]) || !graphic(key[2])) continue;
                const int code = FormIndex(key[4]);
                if (code < 0) continue;
                const std::string_view id = key.substr(0, 4);
                if (list.empty() || list.back().id != id) list.push_back({std::string(id), {}});
                list.back().rows[static_cast<std::size_t>(code)] = row.text;
            }
            FamilyIndex& byRow1 = out[static_cast<std::size_t>(MarkerIndex(m))];
            for (Family& f : list) {
                if (f.rows[1].empty()) continue;
                const std::string row1 = f.rows[1];
                byRow1[row1].push_back(std::move(f));
            }
        }
        return out;
    }();
    return index[static_cast<std::size_t>(MarkerIndex(marker))];
}

bool EndsWithAny(std::string_view s, std::initializer_list<std::string_view> suffixes) {
    return std::any_of(suffixes.begin(), suffixes.end(), [s](std::string_view x) { return s.ends_with(x); });
}

bool Contains(std::string_view set, std::string_view s) { return set.find(s) != std::string_view::npos; }

// The class of a stem written without one.
std::string_view AutoClass(const std::u32string& stem, bool adjective) {
    const std::string s = Utf8(stem);
    const std::string last = Str(stem.back());
    const Jamo j = Decompose(stem.back());
    if (last == "하") return "하다";
    if (last == "되") return "되다";
    if (last == "이" && adjective && stem.size() > 1) return "이다";
    if (j.final == kFinalL) return "ㄹ";
    if (j.final == kFinalH && last != "좋" && (adjective || Contains("렇떻얗갛랗맣", last))) return "ㅎ";
    if (j.final == kFinalB && !Contains("잡입씹접뽑업집꼽좁", last)) return "ㅂ";
    if (j.final == kFinalD && EndsWithAny(s, {"듣", "걷", "묻", "싣", "긷", "붇", "컫", "깨닫", "내닫", "치닫"})) return "ㄷ";
    if (j.final == kFinalS && Contains("짓낫붓잇긋젓잣", last)) return "ㅅ";
    if (last == "르" && stem.size() > 1 && !EndsWithAny(s, {"따르", "치르", "들르", "우러르", "다다르"})) return "르";
    if (last == "푸") return "우";
    if (j.vowel == kVowelEu && j.final == 0) return "으";
    return "규칙";
}

struct Forms {
    std::vector<std::string> past, inf;
    void Both(std::string p, std::string i) {
        past.push_back(std::move(p));
        inf.push_back(std::move(i));
    }
    // stem + 았/었 and stem + 아/어.
    void Suffixed(const std::string& stem, bool bright) {
        const int v = bright ? kVowelA : kVowelEo;
        Both(stem + Compose(kInitialNone, v, kFinalSS), stem + Compose(kInitialNone, v, 0));
    }
};

std::string ClassList() {
    std::string s;
    for (const std::string_view c : kClasses) {
        if (!s.empty()) s += ' ';
        s += c;
    }
    return s;
}

// The accepted past (row 5) and infinitive (row 6) forms of stem in class.
std::string StemForms(const std::u32string& stem, std::string_view cls, Forms& f) {
    const std::size_t n = stem.size();
    const std::string pre = Utf8(std::u32string_view(stem).substr(0, n - 1));
    const std::string whole = Utf8(stem);
    const Jamo j = Decompose(stem[n - 1]);
    const int l = j.initial, v = j.vowel, t = j.final;
    const std::string mismatch = "class " + std::string(cls) + " does not fit " + internal::Quote(whole + "다");
    if (cls == "하다") {
        f.Both(pre + "했", pre + "해");
        f.Both(pre + "하였", pre + "하여");
    } else if (cls == "되다") {
        f.Both(pre + "됐", pre + "돼");
        f.Both(pre + "되었", pre + "되어");
    } else if (cls == "규칙" || cls == "ㄹ") {
        if (cls == "ㄹ" && t != kFinalL) return mismatch;
        int contract = 0;
        switch (v) {
            case kVowelO: contract = kVowelWa; break;
            case kVowelU: contract = kVowelWo; break;
            case kVowelI: contract = kVowelYeo; break;
            case kVowelOe: contract = kVowelWae; break;
        }
        if (t != 0) {
            f.Suffixed(whole, Bright(v));
        } else if (v == kVowelA || v == kVowelEo || v == kVowelAe || v == kVowelE || v == kVowelYeo) {
            f.Both(pre + Compose(l, v, kFinalSS), whole);
            if (v != kVowelA && v != kVowelEo) f.Suffixed(whole, false);
        } else if (v == kVowelEu) {
            return StemForms(stem, "으", f);
        } else if (contract != 0) {
            f.Both(pre + Compose(l, contract, kFinalSS), pre + Compose(l, contract, 0));
            f.Suffixed(whole, Bright(v));
        } else {
            f.Suffixed(whole, Bright(v));
        }
    } else if (cls == "ㅂ") {
        if (t != kFinalB) return mismatch;
        const std::string open = pre + Compose(l, v, 0);
        const std::string last = Str(stem[n - 1]);
        const int w = last == "돕" || last == "곱" ? kVowelWa : kVowelWo;
        f.Both(open + Compose(kInitialNone, w, kFinalSS), open + Compose(kInitialNone, w, 0));
    } else if (cls == "ㄷ") {
        if (t != kFinalD) return mismatch;
        f.Suffixed(pre + Compose(l, v, kFinalL), Bright(v));
    } else if (cls == "ㅅ") {
        if (t != kFinalS) return mismatch;
        f.Suffixed(pre + Compose(l, v, 0), Bright(v));
    } else if (cls == "르") {
        const Jamo p = Decompose(stem[n >= 2 ? n - 2 : 0]);
        if (Str(stem[n - 1]) != "르" || n < 2 || !p.ok) return mismatch;
        const int rv = Bright(p.vowel) ? kVowelA : kVowelEo;
        const std::string head = Utf8(std::u32string_view(stem).substr(0, n - 2)) + Compose(p.initial, p.vowel, kFinalL);
        f.Both(head + Compose(kInitialR, rv, kFinalSS), head + Compose(kInitialR, rv, 0));
    } else if (cls == "러") {
        if (Str(stem[n - 1]) != "르") return mismatch;
        f.Both(whole + "렀", whole + "러");
    } else if (cls == "ㅎ") {
        int front = -1;
        switch (v) {
            case kVowelA: front = kVowelAe; break;
            case kVowelYa: front = kVowelYae; break;
            case kVowelEo: front = kVowelAe; break;
            case kVowelYeo: front = kVowelYe; break;
        }
        if (t != kFinalH || front < 0) return mismatch;
        f.Both(pre + Compose(l, front, kFinalSS), pre + Compose(l, front, 0));
    } else if (cls == "우") {
        if (v != kVowelU || t != 0) return mismatch;
        f.Both(pre + Compose(l, kVowelEo, kFinalSS), pre + Compose(l, kVowelEo, 0));
    } else if (cls == "으") {
        if (v != kVowelEu || t != 0) return mismatch;
        int nv = kVowelEo;
        if (n >= 2) {
            const Jamo p = Decompose(stem[n - 2]);
            if (p.ok && Bright(p.vowel)) nv = kVowelA;
        }
        f.Both(pre + Compose(l, nv, kFinalSS), pre + Compose(l, nv, 0));
    } else {
        return "unknown conjugation class " + internal::Quote(cls) + " (known: " + ClassList() + ")";
    }
    return {};
}

void Set(FamilyRows& rows, char code, std::string s) { rows[static_cast<std::size_t>(FormIndex(code))] = std::move(s); }

const std::string& Get(const FamilyRows& rows, char code) { return rows[static_cast<std::size_t>(FormIndex(code))]; }

// The rows of a family for the final syllables tail of a stem in class: by form code
// '0'..'9', 'A', 'B' for verbs and '1'..'8' for adjectives. past and inf are the
// stem's rows 5 and 6.
FamilyRows Conjugate(const std::u32string& tail, std::string_view cls, const std::string& past, const std::string& inf,
                     bool adjective) {
    const std::size_t n = tail.size();
    const char32_t last = tail[n - 1];
    const std::string pre = Utf8(std::u32string_view(tail).substr(0, n - 1));
    const Jamo j = Decompose(last);
    const int l = j.initial, v = j.vowel, t = j.final;
    const std::string s = Str(last);
    const std::string open = pre + Compose(l, v, 0);
    const auto with = [&](int final) { return pre + Compose(l, v, final); };
    // Vowel stems take ㄴ ㄹ ㅁ ㅂ as finals; consonant stems take 으 forms.
    const auto vowelRows = [](const std::string& base, const std::u32string& b) {
        const Jamo bj = Decompose(b.back());
        const std::string bp = Utf8(std::u32string_view(b).substr(0, b.size() - 1));
        const auto fin = [&](int final) { return bp + Compose(bj.initial, bj.vowel, final); };
        FamilyRows r;
        Set(r, '2', base);
        Set(r, '3', fin(kFinalL));
        Set(r, '4', fin(kFinalN));
        Set(r, '7', fin(kFinalB));
        Set(r, '8', fin(kFinalN));
        Set(r, '9', fin(kFinalB));
        Set(r, 'A', base);
        Set(r, 'B', fin(kFinalM));
        return r;
    };
    const auto consonantRows = [&s](const std::string& base) {
        FamilyRows r;
        Set(r, '2', base + "으");
        Set(r, '3', base + "을");
        Set(r, '4', s + "는");
        Set(r, '7', s + "습");
        Set(r, '8', base + "은");
        Set(r, '9', base + "읍");
        Set(r, 'A', base + "으");
        Set(r, 'B', base + "음");
        return r;
    };
    FamilyRows rows;
    if (cls == "ㄹ") {
        Set(rows, '2', s);
        Set(rows, '3', s);
        Set(rows, '4', pre + Compose(l, v, kFinalN));
        Set(rows, '7', pre + Compose(l, v, kFinalB));
        Set(rows, '8', pre + Compose(l, v, kFinalN));
        Set(rows, '9', pre + Compose(l, v, kFinalB));
        Set(rows, 'A', open);
        Set(rows, 'B', with(kFinalLM));
        Set(rows, '0', open + "는");
    } else if (cls == "ㅂ") {
        const std::u32string u = Runes(open + "우");
        rows = vowelRows(Utf8(u), u);
        Set(rows, '4', s + "는");
        Set(rows, '7', s + "습");
    } else if (cls == "ㄷ") {
        rows = consonantRows(with(kFinalL));
    } else if (cls == "ㅅ") {
        rows = consonantRows(open);
    } else if (cls == "ㅎ") {
        rows = vowelRows(open, Runes(open));
        Set(rows, '4', s + "는");
        Set(rows, '7', s + "습");
    } else if (t != 0) {
        rows = consonantRows(s);
    } else {
        rows = vowelRows(Utf8(tail), tail);
    }
    if (Get(rows, '0').empty()) Set(rows, '0', Utf8(tail) + "는");
    Set(rows, '1', Utf8(tail));
    Set(rows, '5', past);
    Set(rows, '6', inf);
    if (!adjective) return rows;
    FamilyRows adj;
    static constexpr std::pair<char, char> kAdjectiveRows[] = {{'1', '1'}, {'2', '2'}, {'3', '3'}, {'4', '8'},
                                                               {'5', '5'}, {'6', '6'}, {'7', '7'}, {'8', 'B'}};
    for (const auto& [code, from] : kAdjectiveRows) Set(adj, code, Get(rows, from));
    return adj;
}

std::string_view CutPrefix(std::string_view s, std::string_view prefix) {
    return s.starts_with(prefix) ? s.substr(prefix.size()) : s;
}

}  // namespace

std::string_view Families::Generated(std::string_view key) const {
    const auto it = rows_.find(std::string(key));
    return it == rows_.end() ? std::string_view() : std::string_view(it->second);
}

std::string_view Families::SourceRow(std::string_view key) const {
    const std::string_view row = output::SourceMarkerRow(key);
    return row.empty() ? Generated(key) : row;
}

std::string_view Families::DollarRow(std::string_view key) const {
    const std::string_view row = output::DollarMarkerRow(key);
    return row.empty() ? Generated(key) : row;
}

std::string_view Families::PercentRow(std::string_view key) const {
    const std::string_view row = output::PercentMarkerRow(key);
    return row.empty() ? Generated(key) : row;
}

std::string Families::Generate(char marker, const FamilyRows& rows, std::string& id) {
    const int m = MarkerIndex(marker);
    if (m < 0) return "unknown marker " + internal::Quote(std::string_view(&marker, 1));
    std::string sig(1, marker);
    for (const std::string& row : rows) {
        sig += '\0';
        sig += row;
    }
    if (const auto it = ids_.find(sig); it != ids_.end()) {
        id = it->second;
        return {};
    }
    static constexpr std::string_view kSecond = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const std::string_view used = output::MarkerFamilyLetters(marker);
    int n = count_[static_cast<std::size_t>(m)];
    id.clear();
    for (char first = 'A'; first <= 'z' && id.empty(); first++) {
        if (used.find(first) != std::string_view::npos || (first > 'Z' && first < 'a')) continue;
        if (n < static_cast<int>(kSecond.size())) id = {marker, first, kSecond[static_cast<std::size_t>(n)], '@'};
        n -= static_cast<int>(kSecond.size());
    }
    if (id.empty()) return std::string("no free ") + marker + " family ID";
    count_[static_cast<std::size_t>(m)]++;
    ids_.emplace(std::move(sig), id);
    for (std::size_t c = 0; c < rows.size(); c++) {
        if (!rows[c].empty()) rows_.emplace(id + kFormCodes[c], rows[c]);
    }
    return {};
}

std::vector<std::pair<std::string, FamilyRows>> Families::GeneratedFamilies() const {
    std::vector<std::pair<std::string, FamilyRows>> out;
    for (const auto& [sig, id] : ids_) {
        FamilyRows rows;
        for (std::size_t c = 0; c < rows.size(); c++) {
            const auto it = rows_.find(id + std::string(kFormCodes.substr(c, 1)));
            if (it != rows_.end()) rows[c] = it->second;
        }
        out.emplace_back(id, std::move(rows));
    }
    return out;
}

std::size_t Families::MemoryBytes() const {
    std::size_t n = sizeof(Families);
    for (const auto& [k, v] : rows_) n += 64 + k.capacity() + v.capacity();
    for (const auto& [k, v] : ids_) n += 64 + k.capacity() + v.capacity();
    return n;
}

std::string Conjugating(std::string_view lemma, char marker, std::string_view cls, Families& families,
                        std::string& target) {
    if (!lemma.ends_with("다") || lemma.size() == std::string_view("다").size()) {
        return "KR " + internal::Quote(lemma) + " is not a lemma ending in 다";
    }
    const std::u32string stem = Runes(lemma.substr(0, lemma.size() - std::string_view("다").size()));
    const std::size_t n = stem.size();
    if (!Decompose(stem[n - 1]).ok) return "KR " + internal::Quote(lemma) + " does not end in a Hangul syllable before 다";
    const bool explicitClass = !cls.empty();
    if (!explicitClass) cls = AutoClass(stem, marker != '&');
    const std::string head = Utf8(std::u32string_view(stem).substr(0, n - 1));
    const std::string last = Str(stem[n - 1]);
    if (cls == "하다" || cls == "되다") {
        if (last != cls.substr(0, cls.size() - std::string_view("다").size())) {
            return "class " + std::string(cls) + " does not fit " + internal::Quote(lemma);
        }
        std::string_view id;
        const bool become = cls == "되다";
        switch (marker) {
            case '&': id = become ? "&C6@" : "&AV@"; break;
            case '$': id = become ? "$AE@" : "$AB@"; break;
            case '%': id = become ? "%D5@" : "%A1@"; break;
        }
        if (marker == '%' && cls == "하다" && n >= 2 && Contains("아어워", Str(stem[n - 2]))) id = "%D1@";
        target = head + std::string(id);
        return {};
    }
    if (cls == "이다") {
        if (last != "이" || n < 2) return "class 이다 does not fit " + internal::Quote(lemma);
        std::string_view id;
        switch (marker) {
            case '&': id = "&Ac@"; break;
            case '$': id = "$AH@"; break;
            case '%': id = "%D4@"; break;
        }
        target = head + std::string(id);
        return {};
    }
    Forms forms;
    if (std::string err = StemForms(stem, cls, forms); !err.empty()) return err;
    const FamilyIndex& index = StaticFamilies(marker);
    for (std::size_t k = std::min<std::size_t>(3, n); k >= 1; k--) {
        const std::string pre = Utf8(std::u32string_view(stem).substr(0, n - k));
        const auto it = index.find(Utf8(std::u32string_view(stem).substr(n - k)));
        if (it == index.end()) continue;
        const std::vector<Family>& fams = it->second;
        const Family* fit = nullptr;
        std::size_t fitCount = 0;
        std::vector<std::string> distinct;
        for (const Family& f : fams) {
            // Some $ families conjugate like verbs (attributive 는, as in 안되다); an
            // adjective lemma takes them only after 있 and 없.
            if (marker == '$' && Get(f.rows, '4').ends_with("는") && !Get(f.rows, '1').ends_with("있") &&
                !Get(f.rows, '1').ends_with("없")) {
                continue;
            }
            const std::string rows56 = Get(f.rows, '5') + "/" + Get(f.rows, '6');
            if (std::find(distinct.begin(), distinct.end(), rows56) == distinct.end()) distinct.push_back(rows56);
            const auto has = [](const std::vector<std::string>& v, const std::string& s) {
                return std::find(v.begin(), v.end(), s) != v.end();
            };
            if (has(forms.past, pre + Get(f.rows, '5')) && has(forms.inf, pre + Get(f.rows, '6'))) {
                if (!fit) fit = &f;
                fitCount++;
            }
        }
        if (!fit) continue;
        if (!explicitClass && cls == "규칙" && fitCount < fams.size() && distinct.size() > 1) {
            std::string list;
            for (const Family& f : fams) {
                if (!list.empty()) list += ", ";
                list += f.id + " " + Get(f.rows, '5') + "/" + Get(f.rows, '6');
            }
            return "KR " + internal::Quote(lemma) + " matches marker families that conjugate differently (" + list +
                   "): name the class";
        }
        target = pre + fit->id;
        return {};
    }
    const std::size_t cover = cls == "르" ? 2 : 1;
    const std::string prefix = Utf8(std::u32string_view(stem).substr(0, n - cover));
    const FamilyRows rows = Conjugate(stem.substr(n - cover), cls, std::string(CutPrefix(forms.past[0], prefix)),
                                      std::string(CutPrefix(forms.inf[0], prefix)), marker != '&');
    std::string id;
    if (std::string err = families.Generate(marker, rows, id); !err.empty()) return err;
    target = prefix + id;
    return {};
}

}  // namespace hdTrans::userdict
