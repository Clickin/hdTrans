#include "core/filter/filter.h"

#include <algorithm>
#include <climits>
#include <unordered_map>

#include "core/dict/layer.h"
#include "core/filter/ehnd.h"
#include "core/filter/pattern.h"
#include "core/text/utf8.h"

namespace hdTrans::filter {

std::wstring Wide(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const text::Rune r = text::DecodeRune(s.substr(i));
        i += r.size;
        if constexpr (sizeof(wchar_t) == 2) {
            if (r.cp >= 0x10000) {
                out += static_cast<wchar_t>(0xD800 + ((r.cp - 0x10000) >> 10));
                out += static_cast<wchar_t>(0xDC00 + ((r.cp - 0x10000) & 0x3FF));
                continue;
            }
        }
        out += static_cast<wchar_t>(r.cp);
    }
    return out;
}

std::string Narrow(std::wstring_view s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (std::size_t i = 0; i < s.size(); i++) {
        char32_t cp = static_cast<char32_t>(s[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<char32_t>(s[++i]) - 0xDC00);
            }
        }
        if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) cp = text::kReplacement;
        text::AppendRune(out, cp);
    }
    return out;
}

namespace {

// _wtoi: leading blanks, a sign, then digits up to the first other character; 0 for
// none, clamped to int.
int ToInt(std::wstring_view s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == L' ' || (s[i] >= L'\t' && s[i] <= L'\r'))) i++;
    bool negative = false;
    if (i < s.size() && (s[i] == L'+' || s[i] == L'-')) negative = s[i++] == L'-';
    long long v = 0;
    for (; i < s.size() && s[i] >= L'0' && s[i] <= L'9'; i++) {
        v = std::min<long long>(v * 10 + (s[i] - L'0'), static_cast<long long>(INT_MAX) + 1);
    }
    if (negative) v = -v;
    return static_cast<int>(std::clamp<long long>(v, INT_MIN, INT_MAX));
}

// The fields of line (with its "\n", if any) from start: they end at a tab, at "\n", at the
// end, and at the second "/" of "//", which with stop also ends the line; the field
// before "//" keeps its first "/".
std::vector<std::wstring_view> Fields(std::wstring_view line, std::size_t start, bool stop) {
    std::vector<std::wstring_view> out;
    for (std::size_t i = start, prev = start; i <= line.size(); i++) {
        const bool comment = i < line.size() && i > 0 && line[i] == L'/' && line[i - 1] == L'/';
        if (i == line.size() || line[i] == L'\t' || line[i] == L'\n' || comment) {
            out.push_back(line.substr(prev, i - prev));
            prev = i + 1;
            if (comment && stop) break;
        }
    }
    return out;
}

// Splits text into lines that keep their "\n".
std::vector<std::wstring_view> LinesOf(std::wstring_view text) {
    std::vector<std::wstring_view> out;
    for (std::size_t pos = 0; pos < text.size();) {
        const std::size_t nl = text.find(L'\n', pos);
        const std::size_t end = nl == std::wstring_view::npos ? text.size() : nl + 1;
        out.push_back(text.substr(pos, end - pos));
        pos = end;
    }
    return out;
}

// The text after prefix and before the next tab, and the position after that tab; false
// without a tab.
bool Name(std::wstring_view line, std::size_t at, std::wstring& name, std::size_t& next) {
    const std::size_t tab = line.find(L'\t', at);
    if (tab == std::wstring_view::npos) return false;
    name = std::wstring(line.substr(at, tab - at));
    next = tab + 1;
    return true;
}

bool StartsAt(std::wstring_view line, std::size_t at, std::wstring_view s) {
    return at <= line.size() && line.substr(at).starts_with(s);
}

// A row as read, before its patterns are compiled.
struct ParsedRow {
    std::wstring src, dest, aux;
    int layer = 0, regex = 0;
    bool hasAux = false;
    std::wstring gate, sets;
    bool on = false;
    int kind = 0;  // 0 plain, 1 guard, 2 guarded
    std::string file;
    int line = 0;
};

// The builder of the toggles: names to numbers, and the declared states.
struct ToggleTable {
    std::unordered_map<std::wstring, int32_t> ids;
    std::vector<char> states;
    std::vector<bool> declared;

    int32_t Id(const std::wstring& name) {
        const auto [it, added] = ids.emplace(name, static_cast<int32_t>(states.size()));
        if (added) {
            states.push_back(0);
            declared.push_back(false);
        }
        return it->second;
    }
    // The first declaration of a name holds.
    void Declare(const std::wstring& name, bool on) {
        const int32_t id = Id(name);
        if (declared[static_cast<std::size_t>(id)]) return;
        declared[static_cast<std::size_t>(id)] = true;
        states[static_cast<std::size_t>(id)] = on;
    }
};

// Reads the rows of a filter file; declarations go to toggles.
void ReadRows(const Source& source, std::vector<ParsedRow>& rows, ToggleTable& toggles, std::vector<Error>& errors) {
    const std::wstring text = Wide(source.text);
    int number = 0;
    for (const std::wstring_view line : LinesOf(text)) {
        number++;
        if (line.starts_with(L"//") || (source.native && line.starts_with(L"#"))) continue;
        ParsedRow row;
        row.file = source.name;
        row.line = number;
        std::size_t start = 0;
        bool malformed = false;
        if (line.starts_with(L"_TGL\t")) {
            std::size_t next = 0;
            if (StartsAt(line, 5, L"_DECR:")) {
                std::wstring name;
                if (Name(line, 11, name, next) && (StartsAt(line, next, L"_ON") || StartsAt(line, next, L"_OFF"))) {
                    toggles.Declare(name, StartsAt(line, next, L"_ON"));
                    continue;
                }
                malformed = true;
            } else if (StartsAt(line, 5, L"_CONN:")) {
                malformed = !Name(line, 11, row.gate, next);
                start = next;
                if (!malformed && StartsAt(line, next, L"_ON:")) {
                    malformed = !Name(line, next + 4, row.sets, start);
                    row.on = true;
                } else if (!malformed && StartsAt(line, next, L"_OFF:")) {
                    malformed = !Name(line, next + 5, row.sets, start);
                } else if (!malformed && StartsAt(line, next, L"_SkipLayer\t")) {
                    row.kind = 1;
                    start = next + 11;
                }
            } else if (StartsAt(line, 5, L"_ON:")) {
                malformed = !Name(line, 9, row.sets, start);
                row.on = true;
            } else if (StartsAt(line, 5, L"_OFF:")) {
                malformed = !Name(line, 10, row.sets, start);
            }
        } else if (line.starts_with(L"_SkipLayer\t")) {
            row.kind = 1;
            start = 11;
        } else if (line.starts_with(L"_skip_sub\t")) {
            row.kind = 2;
            start = 10;
        }
        if (malformed) {
            errors.push_back({source.name, number, "a _TGL line without its tab-separated name"});
            continue;
        }
        const std::vector<std::wstring_view> f = Fields(line, start, true);
        if (f.size() < 3) continue;
        row.src = f[0];
        row.dest = f[1];
        row.layer = ToInt(f[2]);
        if (f.size() > 3) row.regex = ToInt(f[3]);
        if (f.size() > 4 && f[4].starts_with(L"_AUX:")) {
            row.hasAux = true;
            row.aux = f[4].substr(5);
        }
        rows.push_back(std::move(row));
    }
}

}  // namespace

std::shared_ptr<const Filters> Build(const std::vector<Source>& sources, std::vector<Error>& errors) {
    if (sources.empty()) return nullptr;
    auto out = std::make_shared<Filters>();
    Filters& fs = *out;
    ToggleTable toggles;
    std::vector<ParsedRow> rows[2];
    struct ParsedCondition {
        Stage stage;
        int layer;
        std::wstring cond;
        std::string file;
        int line;
    };
    std::vector<ParsedCondition> conditions;
    dict::internal::Layer user(false);
    std::vector<UserRow> userRows;

    for (const Source& s : sources) {
        switch (s.kind) {
        case Source::Kind::PreFilter:
            ReadRows(s, rows[0], toggles, errors);
            break;
        case Source::Kind::PostFilter:
            ReadRows(s, rows[1], toggles, errors);
            break;
        case Source::Kind::SkipLayer: {
            const std::wstring text = Wide(s.text);
            int number = 0;
            for (const std::wstring_view line : LinesOf(text)) {
                number++;
                if (line.starts_with(L"//")) continue;
                const std::vector<std::wstring_view> f = Fields(line, 0, false);
                if (f.size() < 2 || !(f[0].starts_with(L"PRE") || f[0].starts_with(L"POST"))) continue;
                conditions.push_back({f[0].starts_with(L"PRE") ? Stage::Pre : Stage::Post, ToInt(f[1]),
                                      f.size() > 2 ? std::wstring(f[2]) : std::wstring(), s.name, number});
            }
            break;
        }
        case Source::Kind::UserDict: {
            std::vector<UserRow> r = ParseUserDict(s.text, s.name, errors);
            std::move(r.begin(), r.end(), std::back_inserter(userRows));
            break;
        }
        }
    }

    // A regex compiles only after CheckPattern accepts it.
    auto compile = [&](const std::wstring& pattern, Matcher& matcher, int32_t& regex, int32_t& literal,
                       const std::string& file, int line) {
        std::wstring required;
        std::string why;
        if (!CheckPattern(pattern, required, why)) {
            errors.push_back({file, line, "regex rejected (" + why + "): " + Narrow(pattern)});
            return false;
        }
        regex = static_cast<int32_t>(fs.regexes_.size());
        fs.regexes_.emplace_back(pattern);
        literal = required.empty() ? -1 : matcher.Add(required);
        return true;
    };

    for (int stage = 0; stage < 2; stage++) {
        Filters::Program& p = stage == 0 ? fs.pre_ : fs.post_;
        std::stable_sort(rows[stage].begin(), rows[stage].end(),
                         [](const ParsedRow& a, const ParsedRow& b) { return a.layer < b.layer; });
        for (ParsedRow& r : rows[stage]) {
            Filters::Rule rule;
            rule.layer = r.layer;
            rule.kind = static_cast<Filters::Kind>(r.kind);
            rule.check = r.dest == L"_MATCH_CHECK";
            if (!r.gate.empty()) rule.gate = toggles.Id(r.gate);
            if (!r.sets.empty()) rule.sets = toggles.Id(r.sets);
            rule.on = r.on;
            if (r.regex != 0) {
                if (!compile(r.src, p.matcher, rule.regex, rule.pattern, r.file, r.line)) continue;
            } else if (r.src.empty()) {
                errors.push_back({r.file, r.line, "a literal row with an empty source"});
                continue;
            } else {
                rule.pattern = p.matcher.Add(r.src);
            }
            if (r.hasAux && !r.aux.empty()) rule.aux = p.matcher.Add(r.aux);
            rule.src = std::move(r.src);
            rule.dest = std::move(r.dest);
            p.rules.push_back(std::move(rule));
        }
        for (std::size_t i = 0; i < p.rules.size(); i++) {
            p.rules[i].blockEnd = i + 1 == p.rules.size() || p.rules[i + 1].kind != Filters::Kind::Guarded;
        }
    }
    std::stable_sort(conditions.begin(), conditions.end(),
                     [](const ParsedCondition& a, const ParsedCondition& b) { return a.layer < b.layer; });
    for (const ParsedCondition& c : conditions) {
        Filters::Program& p = c.stage == Stage::Pre ? fs.pre_ : fs.post_;
        Filters::Condition cond;
        cond.layer = c.layer;
        if (compile(c.cond, p.matcher, cond.regex, cond.pattern, c.file, c.line)) p.conditions.push_back(cond);
    }
    fs.pre_.matcher.Build();
    fs.post_.matcher.Build();
    fs.defaults_ = toggles.states;

    for (const UserRow& r : userRows) {
        const int32_t code = fs.codes_.Intern(r.phrase ? "A9D0" : "I110", r.attr, "", "GN");
        user.Add(r.key, r.target, code, 0, 0, 0);
    }
    fs.user_ = user.Build(fs.codes_);
    for (int32_t c = 0; c < static_cast<int32_t>(fs.codes_.Size()); c++) {
        fs.codes_.SetWithXXX(c);
        fs.codes_.SetAsPlain(c);
    }
    return out;
}

namespace {

// Replaces every src in text, left to right, resuming after each replacement. Returns
// whether the row fires: it does unless the last replacement ends at 0.
bool ReplaceAll(std::wstring& text, std::wstring_view src, std::wstring_view dest) {
    std::size_t offset = 0;
    for (std::size_t pos; (pos = text.find(src, offset)) != std::wstring::npos;) {
        text.replace(pos, src.size(), dest);
        offset = pos + dest.size();
    }
    return offset != 0;
}

}  // namespace

void Filters::Apply(Stage stage, std::string& line, Toggles& toggles) const {
    const Program& p = stage == Stage::Pre ? pre_ : post_;
    if (line.empty() || p.rules.empty()) return;
    std::wstring text = Wide(line);
    Run(p, text, toggles);
    line = Narrow(text);
}

void Filters::Run(const Program& p, std::wstring& text, Toggles& toggles) const {
    std::vector<uint32_t> seen(p.matcher.Size());
    uint32_t stamp = 0;
    bool dirty = true;
    auto has = [&](int32_t pattern) {
        if (pattern < 0) return true;
        if (dirty) {
            p.matcher.Scan(text, seen, ++stamp);
            dirty = false;
        }
        return seen[static_cast<std::size_t>(pattern)] == stamp;
    };
    auto search = [&](int32_t regex, int32_t pattern) {
        return has(pattern) && std::regex_search(text, regexes_[static_cast<std::size_t>(regex)]);
    };

    int layer = -1;
    bool skipLayer = false, guard = false;
    for (const Rule& r : p.rules) {
        if (r.layer != layer) {
            layer = r.layer;
            skipLayer = false;
            const auto first = std::lower_bound(p.conditions.begin(), p.conditions.end(), layer,
                                                [](const Condition& c, int l) { return c.layer < l; });
            for (auto c = first; c != p.conditions.end() && c->layer == layer && !skipLayer; ++c) {
                skipLayer = !search(c->regex, c->pattern);
            }
        }
        if (skipLayer) continue;
        if (r.gate >= 0 && !toggles[static_cast<std::size_t>(r.gate)]) continue;
        if (r.kind == Kind::Guarded) {
            if (!guard) continue;
            if (r.blockEnd) guard = false;
        }
        bool fired = false;
        if (r.regex < 0) {
            // Guarded and check rows ignore _AUX.
            const bool aux = r.kind == Kind::Guarded || r.check || has(r.aux);
            if (r.check) {
                fired = has(r.pattern);
            } else if (aux && has(r.pattern)) {
                fired = ReplaceAll(text, r.src, r.dest);
                dirty = true;
            }
        } else if (has(r.aux)) {
            const std::wregex& re = regexes_[static_cast<std::size_t>(r.regex)];
            if (r.check) {
                fired = search(r.regex, r.pattern);
            } else if (search(r.regex, r.pattern)) {
                std::wstring replaced = std::regex_replace(text, re, r.dest);
                fired = replaced != text;
                if (fired) {
                    text = std::move(replaced);
                    dirty = true;
                }
            }
        }
        if (!fired) continue;
        if (r.sets >= 0) toggles[static_cast<std::size_t>(r.sets)] = r.on;
        if (r.kind == Kind::Guard) guard = true;
    }
}

}  // namespace hdTrans::filter
