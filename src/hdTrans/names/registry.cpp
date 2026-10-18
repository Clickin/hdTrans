#include "hdTrans/names/registry.h"

#include <algorithm>
#include <array>
#include <set>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/names/chars.h"
#include "hdTrans/names/labels.h"
#include "hdTrans/names/readings.h"
#include "hdTrans/names/ruby.h"
#include "hdTrans/text/kana.h"

namespace hdTrans::names {

using namespace internal;

namespace {

// Thresholds of the evidence score.
constexpr double kThreshold = 2.0;
constexpr double kSingleThreshold = 3.0;  // single-kanji surfaces without a label
constexpr int kHonorificCap = 4;          // honorific occurrences counted per surface

// The byte offsets of the code point starts of s, in order.
std::vector<std::size_t> RuneStarts(std::string_view s) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < s.size(); i += RuneAt(s, i).size) out.push_back(i);
    return out;
}

bool IsRuneStartByte(char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }

// Whether text ends with suffix as a whole string with a non-empty head.
bool HasSuffixOf(std::string_view text, std::string_view suffix) {
    return text.size() > suffix.size() && text.ends_with(suffix);
}

// Whether lst holds s.
bool Contains(const std::vector<std::string_view>& lst, std::string_view s) {
    for (const std::string_view w : lst) {
        if (w == s) return true;
    }
    return false;
}

// Whether the next text after a name ends the name: no hiragana, or a particle or an
// honorific.
bool NameEnd(std::string_view text) {
    if (text.empty()) return true;
    const char32_t c = RuneAt(text, 0).cp;
    if (!IsHiragana(c) || InRunes(c, "はがをのにへともやでかだ")) return true;
    for (const std::string_view h : Honorifics()) {
        if (text.starts_with(h)) return true;
    }
    return false;
}

// The non-empty fields of s separated by ・ or ＝.
std::vector<std::string> ChainParts(std::string_view s) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size();) {
        const bool sep = i < s.size() && (s.substr(i).starts_with("・") || s.substr(i).starts_with("＝"));
        if (i == s.size() || sep) {
            if (i > start) parts.emplace_back(s.substr(start, i - start));
            const std::size_t width = sep ? std::string_view("・").size() : 0;
            i += width == 0 ? 1 : width;
            start = i;
            continue;
        }
        i++;
    }
    return parts;
}

}  // namespace

Registry::Registry(const dict::Snapshot* snap) : snap_(snap) {
    internal::AddBuiltinReadings(readings_);
}

void Registry::SetName(std::string_view t, std::string_view kr) {
    if (!t.empty() && IsHangulText(kr)) {
        scoresValid_ = false;
        fixed_[std::string(t)] = std::string(kr);
    }
}

void Registry::Add(std::string_view rawLine) {
    const std::string_view line0 = TrimCR(rawLine);
    if (TrimSpace(line0).starts_with("//")) return;
    scoresValid_ = false;
    std::string line(line0);
    for (std::size_t from = 0; from <= line.size();) {
        RubyMatch m;
        if (!FindRuby(line, from, m)) break;
        const std::string base(m.base), kana(m.reading);
        if (IsKanaText(kana)) {
            gloss_[base]++;
            if (reading_[base].empty()) reading_[base] = kana;
        }
        from = m.end;
    }
    line = StripRuby(line);
    bool script = false;
    if (const std::string label = Label(line, script); !label.empty()) {
        if (script) {
            labels_[label]++;
        } else {
            prefixes_[label]++;
        }
    }
    const std::vector<Run> runs = RunsOf(line);
    std::size_t chain = 0;  // first run of the current katakana chain
    for (std::size_t i = 0; i < runs.size(); i++) {
        const Run& run = runs[i];
        if (run.kanji) {
            runs_[std::string(run.text)]++;
            if (NameEnd(line.substr(run.end))) ended_[std::string(run.text)]++;
        } else {
            kana_[std::string(run.text)]++;
        }
        bool linked = false;
        if (i + 1 < runs.size()) {
            const Run& next = runs[i + 1];
            if (run.kanji && !next.kanji && next.start == run.end) {
                joined_[{std::string(run.text), "", std::string(next.text)}]++;
            } else if (!run.kanji && !next.kanji && next.start == run.end + std::string_view("・").size()) {
                const std::string_view sep = std::string_view(line).substr(run.end, next.start - run.end);
                linked = sep == "・" || sep == "＝";
            }
        }
        if (!linked) {
            if (chain < i) chains_[std::string(line.substr(runs[chain].start, run.end - runs[chain].start))]++;
            chain = i + 1;
        }
        if (const std::string t = HonorificRun(line, run); !t.empty()) hon_[t]++;
    }
    for (std::size_t from = 0; from <= line.size();) {
        RubyMatch m;
        if (!FindGloss(line, from, m)) break;
        const std::string base(m.base);
        gloss_[base]++;
        if (reading_[base].empty()) {
            std::string r;
            for (std::size_t i = 0; i < m.reading.size();) {
                const std::string_view rest = m.reading.substr(i);
                if (rest.starts_with("・")) {
                    i += std::string_view("・").size();
                } else if (rest.starts_with(" ") || rest.starts_with("　")) {
                    i += rest.starts_with(" ") ? 1 : std::string_view("　").size();
                } else {
                    r += m.reading[i];
                    i++;
                }
            }
            reading_[base] = r;
        }
        from = m.end;
    }
}

std::vector<std::string> Registry::Names() {
    std::vector<std::string> out;
    for (const auto& [t, n] : Accepted()) out.push_back(t);
    return out;
}

const std::map<std::string, Name>& Registry::Accepted() {
    if (scoresValid_) return scores_;
    std::map<std::string, Name> all;
    std::set<std::string> labelled;
    auto addLabel = [&](const std::string& raw) {
        for (const std::string& p : LabelParts(raw)) {
            std::string x;
            if (ParentLabel(p, x)) {
                all[x].score++;
                all[x].person = true;
                continue;
            }
            if (p.empty() || RoleLike(p) || LabelRole(p) || !AllRunes(p, IsNameRune)) continue;
            labelled.insert(p);
        }
    };
    for (const auto& [raw, n] : labels_) addLabel(raw);
    for (const auto& [raw, n] : prefixes_) {
        if (n >= 3) addLabel(raw);
    }
    for (const std::string& t : labelled) {
        Name& n = all[t];
        n.label = true;
        n.person = true;
        n.score += Flags(t).role ? 1.5 : 3;
    }
    for (const auto& [t, c] : hon_) {
        const DictFlags f = Flags(t);
        double w = 1.0;
        if (f.role) {
            w = 0;
        } else if (f.common && !f.person && !f.city && PersonName(t).empty()) {
            w = 0.5;
        }
        Name& n = all[t];
        n.score += static_cast<double>(std::min(c, kHonorificCap)) * w;
        n.person = true;
    }
    for (const auto& [t, c] : gloss_) {
        Name& n = all[t];
        n.score += 2 * static_cast<double>(c);
        n.person = true;
    }
    for (const auto& [t, c] : runs_) {
        const auto rd = readings_.find(t);
        if (rd != readings_.end() && !rd->second.empty() && RuneCount(t) >= 2 && Flags(t).records == 0) {
            all[t].score += static_cast<double>(std::min(c, 2));
        }
    }
    std::set<std::string> att;
    for (const auto& [t, n] : all) {
        if (n.score >= kThreshold) att.insert(t);
    }
    for (const auto& [t, kr] : fixed_) att.insert(t);
    for (const auto& [x, c] : hon_) {
        (void)c;
        if (att.count(x) || RuneCount(x) < 2 || !AllRunes(x, IsKatakana) || Flags(x).role) continue;
        for (const auto& [t, kc] : kana_) {
            if (t.size() > x.size() && t.starts_with(x) && (att.count(t) || (kc >= 2 && Flags(t).records == 0))) {
                Name& n = all[x];
                n.score = std::max(n.score, kThreshold);
                n.person = true;
                att.insert(x);
                break;
            }
        }
    }
    auto surnameLike = [&](const std::string& x) {
        const DictFlags f = Flags(x);
        const std::size_t k = RuneCount(x);
        return k >= 1 && k <= 3 && AllRunes(x, IsKanji) && (f.surname || f.records == 0);
    };
    auto full = [&](const std::string& x, const std::string& y, const std::string& joined, int c) {
        Name& s = all[x];
        s.score += c;
        s.person = true;
        s.surname = true;
        Name& j = all[joined];
        j.score += kThreshold;
        j.person = true;
        j.hasParts = true;
        j.parts = {x, y};
    };
    for (const auto& [t, c] : runs_) {
        for (const std::string& y : att) {
            if (HasSuffixOf(t, y)) {
                const std::string x = t.substr(0, t.size() - y.size());
                if (!x.empty() && surnameLike(x)) full(x, y, t, c);
            }
        }
    }
    for (const auto& [k, c] : joined_) {
        const std::string& x = k[0];
        const std::string& y = k[2];
        if (att.count(y) && surnameLike(x)) full(x, y, x + y, c);
    }
    for (const auto& [t, c] : ended_) {
        const auto [x, y] = GivenSplit(t);
        if (y.empty()) continue;
        full(x, y, t, c);
        Name& g = all[y];
        g.score = std::max(g.score, kSingleThreshold);
        g.person = true;
        g.given = true;
    }
    for (const auto& [t, c] : kana_) {
        if (c < 2 || att.count(t) || Flags(t).records > 0) continue;
        for (const std::string& x : att) {
            if (x.size() < t.size() && t.starts_with(x) && RuneCount(x) >= 2 && AllRunes(x, IsKatakana)) {
                all[t].score += kThreshold;
                all[t].person = true;
                break;
            }
        }
    }
    for (const auto& [t, c] : chains_) {
        const std::vector<std::string> parts = ChainParts(t);
        double score = static_cast<double>(c);
        bool fresh = false;
        for (const std::string& p : parts) {
            if (Flags(p).records == 0 && RuneCount(p) >= 2) fresh = true;
        }
        if (fresh) {
            score = std::max(score, kThreshold);
        } else {
            bool any = false;
            for (const std::string& p : parts) any = any || att.count(p) > 0;
            if (!any) continue;
        }
        for (const std::string& p : parts) {
            all[p].score += score;
            all[p].person = true;
        }
        Name& j = all[t];
        j.score += kThreshold;
        j.person = true;
        j.hasParts = true;
        j.parts = parts;
    }
    scores_.clear();
    for (const auto& [t, n] : all) {
        double need = kThreshold;
        if (RuneCount(t) == 1 && AllRunes(t, IsKanji) && !n.label) need = kSingleThreshold;
        if (AllRunes(t, IsHiragana) && !n.label) continue;
        if (n.score >= need) scores_[t] = n;
    }
    scoresValid_ = true;
    for (const std::string& t : Names()) {
        Name& n = scores_[t];
        if (!n.hasParts) {
            std::vector<std::string> parts;
            if (Split(t, parts)) {
                n.parts = parts;
                n.hasParts = true;
            }
        }
        const auto kanaIt = reading_.find(t);
        if (kanaIt != reading_.end() && !kanaIt->second.empty() && n.parts.size() == 2) {
            const std::string kana = kanaIt->second;
            SplitReading(n.parts[0], n.parts[1], kana);
        }
    }
    return scores_;
}

// The surname of a given-name split (椎名真昼: 椎名, 真昼): a kanji run that is not a
// base word, split at a boundary where the left part is a surname of two or three
// kanji and the right part a given name of one to three, with no base word crossing
// the boundary, and the right part not a person suffix or honorific.
std::pair<std::string, std::string> Registry::GivenSplit(std::string_view t) {
    if (!AllRunes(t, IsKanji) || Flags(t).records > 0) return {"", ""};
    for (const std::size_t i : RuneStarts(t)) {
        const std::string_view x = t.substr(0, i), y = t.substr(i);
        const std::size_t nx = RuneCount(x);
        if (nx < 2 || nx > 3 || RuneCount(y) > 3) continue;
        const DictFlags fx = Flags(x), fy = Flags(y);
        if (!(fx.surname || fx.layerSurname) || fx.common || fx.role) continue;
        if (!(fy.given || fy.layerGiven) || fy.role) continue;
        if (Contains(PersonSuffixes(), y) || Contains(Honorifics(), y) || Crossed(t, i)) continue;
        return {std::string(x), std::string(y)};
    }
    return {"", ""};
}

// Whether a base word of t starts before byte i and ends after it.
bool Registry::Crossed(std::string_view t, std::size_t i) {
    for (const std::size_t j : RuneStarts(t.substr(0, i))) {
        for (std::size_t k = i + 1; k <= t.size(); k++) {
            const bool startK = IsRuneStartByte(t[k - 1]);
            const bool endOk = k == t.size() || IsRuneStartByte(t[k]);
            if (startK && endOk && Flags(t.substr(j, k - j)).common) return true;
        }
    }
    return false;
}

// The surname and given name of a full name t: at the boundary between its kanji and
// katakana, or before or after another accepted name when the other part has two
// characters or more. A katakana word is not split.
bool Registry::Split(std::string_view t, std::vector<std::string>& parts) {
    for (const Run& rn : RunsOf(t)) {
        if (rn.start > 0 && rn.end == t.size()) {
            parts = {std::string(t.substr(0, rn.start)), std::string(t.substr(rn.start))};
            return true;
        }
    }
    if (AllRunes(t, IsKatakana)) return false;
    for (const std::size_t i : RuneStarts(t)) {
        if (i == 0) continue;
        const std::string_view x = t.substr(0, i), y = t.substr(i);
        const bool xName = scores_.count(std::string(x)) > 0, yName = scores_.count(std::string(y)) > 0;
        if ((yName && (xName || RuneCount(x) >= 2)) || (xName && RuneCount(y) >= 2)) {
            parts = {std::string(x), std::string(y)};
            return true;
        }
    }
    return false;
}

// Gives the parts of a full name the readings of its whole, when the other part is known.
void Registry::SplitReading(const std::string& x, const std::string& y, const std::string& kana) {
    if (!reading_[x].empty() && !reading_[y].empty()) return;
    auto known = [&](const std::string& part) -> std::string {
        const auto r = reading_.find(part);
        if (r != reading_.end() && !r->second.empty()) return text::FromKana(r->second, preset);
        const auto v = readings_.find(part);
        if (v != readings_.end() && !v->second.empty()) {
            if (IsKanaText(v->second)) return text::FromKana(v->second, preset);
            return v->second;
        }
        if (!AllRunes(part, IsKanji)) return text::FromKana(part, preset);
        return "";
    };
    // The longest tail that renders as the known given name, else the longest head that
    // renders as the known surname.
    const std::string_view rs = kana;
    std::vector<std::size_t> at = RuneStarts(rs);
    at.push_back(rs.size());
    const std::array<text::Preset, 4> presets = {preset, text::Preset::Fan, text::Preset::FanShort, text::Preset::Official};
    const std::string h = known(y);
    if (!h.empty() && reading_[x].empty()) {
        for (std::size_t idx = 1; idx + 1 < at.size(); idx++) {
            for (const text::Preset p : presets) {
                if (text::FromKana(rs.substr(at[idx]), p) == h) {
                    reading_[x] = std::string(rs.substr(0, at[idx]));
                    return;
                }
            }
        }
    }
    const std::string sh = known(x);
    if (!sh.empty() && reading_[y].empty()) {
        for (std::size_t idx = at.size() - 1; idx-- > 1;) {
            for (const text::Preset p : presets) {
                if (text::FromKana(rs.substr(0, at[idx]), p) == sh) {
                    reading_[y] = std::string(rs.substr(at[idx]));
                    return;
                }
            }
        }
    }
}

bool Registry::Named(std::string_view t) {
    const std::string key(t);
    if (Accepted().count(key) > 0) return true;
    const auto h = hon_.find(key), g = gloss_.find(key);
    if ((h != hon_.end() && h->second > 0) || (g != gloss_.end() && g->second > 0)) return true;
    for (const auto* m : {&labels_, &prefixes_}) {
        for (const auto& [k, n] : *m) {
            for (const std::string& p : LabelParts(k)) {
                if (p == key) return true;
            }
        }
    }
    for (const auto& [k, n] : chains_) {
        for (const std::string& p : LabelParts(k)) {
            if (p == key) return true;
        }
    }
    return false;
}

std::vector<std::string> Registry::NamedSurfaces() {
    std::set<std::string> named;
    for (const auto* m : {&hon_, &gloss_}) {
        for (const auto& [t, n] : *m) {
            if (n > 0) named.insert(t);
        }
    }
    for (const auto* m : {&labels_, &prefixes_}) {
        for (const auto& [k, n] : *m) {
            for (const std::string& t : LabelParts(k)) named.insert(t);
        }
    }
    for (const auto& [k, n] : chains_) {
        for (const std::string& t : LabelParts(k)) named.insert(t);
    }
    return std::vector<std::string>(named.begin(), named.end());
}

std::string Registry::Key() {
    std::string out = Dict();
    out.push_back('\0');
    bool first = true;
    for (const std::string& t : Names()) {
        if (!first) out += "\t";
        out += t;
        first = false;
    }
    out.push_back('\0');
    first = true;
    for (const std::string& t : NamedSurfaces()) {
        if (!first) out += "\t";
        out += t;
        first = false;
    }
    out.push_back('\0');
    for (const auto& [a, v] : actors_) {
        out += a + "=" + v + "\t";
    }
    return out;
}

std::vector<Occurrence> Registry::Occurrences(std::string_view line) {
    const std::map<std::string, Name> acc = Accepted();
    std::vector<Occurrence> out;
    if (acc.empty()) return out;
    std::map<char32_t, std::vector<std::string>> byFirst;
    for (const auto& [t, n] : acc) byFirst[RuneAt(t, 0).cp].push_back(t);
    for (auto& [c, list] : byFirst) {
        std::stable_sort(list.begin(), list.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    }
    const std::vector<Run> runs = RunsOf(line);
    auto okRight = [&](std::string_view p) {
        if (p.empty() || acc.count(std::string(p)) > 0) return true;
        for (const std::string_view k : TitleSuffixes()) {
            if (!p.starts_with(k)) continue;
            const std::string_view rest = p.substr(k.size());
            if (rest.empty() || acc.count(std::string(rest)) > 0) return true;
        }
        return false;
    };
    for (std::size_t i = 0; i < line.size();) {
        const text::Rune c = RuneAt(line, i);
        std::string found;
        const auto list = byFirst.find(c.cp);
        if (list != byFirst.end()) {
            for (const std::string& t : list->second) {
                if (line.substr(i).starts_with(t)) {
                    found = t;
                    break;
                }
            }
        }
        if (found.empty()) {
            i += c.size;
            continue;
        }
        const std::size_t a = i, b = i + found.size();
        i = b;
        bool ok = true;
        for (const Run& rn : runs) {
            if (rn.start <= a && b <= rn.end) {
                const std::string_view left = line.substr(rn.start, a - rn.start), right = line.substr(b, rn.end - b);
                ok = (left.empty() || acc.count(std::string(left)) > 0) && okRight(right);
                break;
            }
        }
        if (AllRunes(found, IsHiragana)) {
            ok = a == 0 || !IsHiragana(internal::RuneBefore(line, a).cp);
        }
        if (ok) out.push_back(Occurrence{a, b, found, Guarded(line, a, b)});
    }
    return out;
}

}  // namespace hdTrans::names
