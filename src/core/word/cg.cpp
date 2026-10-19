#include "core/word/cg.h"

#include <algorithm>

#include "core/dict/builtin.h"
#include "core/dict/tsv.h"
#include "core/word/flush.h"
#include "core/word/worker.h"

namespace hdTrans::word {

namespace {

std::vector<std::string_view> Words(std::string_view s) {
    std::vector<std::string_view> out;
    while (!s.empty()) {
        const std::size_t space = s.find(' ');
        if (space != 0) out.push_back(s.substr(0, space));
        if (space == std::string_view::npos) break;
        s.remove_prefix(space + 1);
    }
    return out;
}

CgLiteral ParseLiteral(std::string_view s) {
    CgLiteral lit;
    if (s.starts_with('!')) {
        lit.negated = true;
        s.remove_prefix(1);
    }
    std::string text;
    for (std::size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 's') {
            text += ' ';
            i++;
        } else {
            text += s[i];
        }
    }
    std::vector<std::string> alternatives{text};
    const std::size_t open = text.find('{'), close = text.find('}');
    if (open != std::string::npos && close != std::string::npos && close > open) {
        alternatives.clear();
        std::string_view inner = std::string_view(text).substr(open + 1, close - open - 1);
        while (true) {
            const std::size_t comma = inner.find(',');
            alternatives.push_back(text.substr(0, open) + std::string(inner.substr(0, comma)) + text.substr(close + 1));
            if (comma == std::string_view::npos) break;
            inner.remove_prefix(comma + 1);
        }
    }
    for (std::string& a : alternatives) {
        const bool prefix = !a.empty() && a.back() == '*';
        if (prefix) a.pop_back();
        lit.alternatives.push_back({prefix, std::move(a)});
    }
    return lit;
}

// Whether lit holds for a candidate with the sorted features.
bool Holds(const CgLiteral& lit, std::span<const std::string> sorted, bool chosen) {
    bool v = false;
    for (const CgLiteral::Alternative& a : lit.alternatives) {
        if (a.text == "chosen") {
            v = chosen;
        } else {
            const auto it = std::lower_bound(sorted.begin(), sorted.end(), a.text);
            v = it != sorted.end() && (a.prefix ? it->starts_with(a.text) : *it == a.text);
        }
        if (v) break;
    }
    return v != lit.negated;
}

}  // namespace

std::vector<CgRule> ParseCgRules(std::string_view text) {
    std::vector<CgRule> rules;
    dict::TsvReader reader(text, "#");
    while (reader.Next()) {
        const auto& f = reader.Fields();
        if (f.size() < 2 || (f[1] != "REMOVE" && f[1] != "SELECT")) continue;
        CgRule r;
        for (const char c : f[0]) {
            if (c >= '0' && c <= '9') r.tier = r.tier * 10 + (c - '0');
        }
        r.select = f[1] == "SELECT";
        if (f.size() > 2) {
            for (const std::string_view w : Words(f[2])) r.target.push_back(ParseLiteral(w));
        }
        if (f.size() > 3) {
            for (std::string_view w : Words(f[3])) {
                CgCondition c;
                if (w == "kept") {
                    c.kind = CgCondition::Kind::Kept;
                } else {
                    if (w.starts_with("any:")) c.kind = CgCondition::Kind::Any, w.remove_prefix(4);
                    else if (w.starts_with("none:")) c.kind = CgCondition::Kind::None, w.remove_prefix(5);
                    c.literal = ParseLiteral(w);
                }
                r.condition.push_back(std::move(c));
            }
        }
        if (f.size() > 4) r.note = f[4];
        rules.push_back(std::move(r));
    }
    std::stable_sort(rules.begin(), rules.end(), [](const CgRule& a, const CgRule& b) { return a.tier < b.tier; });
    return rules;
}

std::span<const CgRule> BuiltinCgRules(bool hand, bool learned) {
    static const std::vector<CgRule> handRules = ParseCgRules(dict::ExtraBuiltinText("cg_rules.tsv"));
    static const std::vector<CgRule> learnedRules = ParseCgRules(dict::ExtraBuiltinText("cg_learned.tsv"));
    static const std::vector<CgRule> both = [] {
        std::string text = dict::ExtraBuiltinText("cg_rules.tsv");
        if (!text.empty() && text.back() != '\n') text += '\n';
        return ParseCgRules(text + dict::ExtraBuiltinText("cg_learned.tsv"));
    }();
    if (hand && learned) return both;
    if (hand) return handRules;
    if (learned) return learnedRules;
    return {};
}

int CgReplacement(const MisreadDecision& d, std::span<const CgRule> rules) {
    const Selection choice = d.kind == MisreadDecisionKind::Word ? d.item->choice : d.item->directChoice;
    if (rules.empty() || !MisreadReplaceable(choice.layer)) return -1;
    const std::size_t n = d.candidates.size();
    std::vector<std::vector<std::string>> sorted(n);
    for (std::size_t i = 0; i < n; i++) {
        sorted[i] = d.candidates[i].features;
        std::sort(sorted[i].begin(), sorted[i].end());
    }
    std::vector<bool> live(n, true);
    std::size_t liveCount = n;
    auto matches = [&](const std::vector<CgLiteral>& lits, std::size_t i) {
        return std::all_of(lits.begin(), lits.end(), [&](const CgLiteral& l) { return Holds(l, sorted[i], i == 0); });
    };
    auto conditionHolds = [&](const CgRule& r) {
        for (const CgCondition& c : r.condition) {
            bool ok = false;
            switch (c.kind) {
                case CgCondition::Kind::Kept: ok = live[0]; break;
                case CgCondition::Kind::Chosen: ok = Holds(c.literal, sorted[0], true); break;
                case CgCondition::Kind::Any:
                case CgCondition::Kind::None: {
                    bool hit = false;
                    for (std::size_t i = 1; i < n && !hit; i++) hit = live[i] && Holds(c.literal, sorted[i], false);
                    ok = (c.kind == CgCondition::Kind::Any) == hit;
                    break;
                }
            }
            if (!ok) return false;
        }
        return true;
    };
    for (std::size_t begin = 0; begin < rules.size();) {
        std::size_t end = begin;
        while (end < rules.size() && rules[end].tier == rules[begin].tier) end++;
        for (bool changed = true; changed && liveCount > 1;) {
            changed = false;
            for (std::size_t k = begin; k < end && liveCount > 1; k++) {
                const CgRule& r = rules[k];
                if (!conditionHolds(r)) continue;
                std::vector<bool> hit(n, false);
                std::size_t hits = 0;
                for (std::size_t i = 0; i < n; i++) {
                    if (live[i] && matches(r.target, i)) hit[i] = true, hits++;
                }
                if (hits == 0 || hits == liveCount) continue;
                for (std::size_t i = 0; i < n; i++) {
                    if (live[i] && hit[i] != r.select) live[i] = false;
                }
                liveCount = r.select ? hits : liveCount - hits;
                changed = true;
            }
        }
        begin = end;
    }
    if (live[0]) return -1;
    for (std::size_t i = 1; i < n; i++) {
        if (live[i]) return static_cast<int>(i);
    }
    return -1;
}

void FilterCg(Flusher& f) {
    const Improvements& on = f.w.settings.improvements;
    const std::span<const CgRule> rules = BuiltinCgRules(on.cgRules, on.cgLearned);
    if (rules.empty()) return;
    const std::vector<MisreadDecision> decisions = MisreadDecisions(f.w.chain, BuiltinMisreadModel());
    struct Change {
        Item* item;
        MisreadDecisionKind kind;
        int slot;
        std::string_view sound;
    };
    std::vector<Change> changes;
    for (const MisreadDecision& d : decisions) {
        const int k = CgReplacement(d, rules);
        if (k < 0) continue;
        const MisreadCandidate& c = d.candidates[static_cast<std::size_t>(k)];
        changes.push_back({d.item, d.kind, c.slot, c.slot < 0 ? std::string_view(d.sound) : std::string_view()});
    }
    const Selection cg{Provenance::Cg, -1};
    for (const Change& c : changes) {
        if (c.kind == MisreadDecisionKind::Ending) c.item->SetDirectIndex(c.slot, cg);
        else if (c.slot >= 0) c.item->SelectCandidate(c.slot, cg);
        else c.item->AssignEntrySurface(c.item->selected, c.sound, cg);
    }
}

}  // namespace hdTrans::word
