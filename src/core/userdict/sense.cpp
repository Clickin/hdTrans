#include "core/userdict/sense.h"

#include "core/userdict/compile.h"

namespace hdTrans::userdict {

namespace {

// The default POS of a sense key.
std::string_view SensePos(std::string_view jp) {
    const std::u32string r = internal::Runes(jp);
    if (r.size() < 2) return "n";
    if (r.back() == U'い') return "adj";
    if (std::u32string_view(U"うくぐすつぬぶむる").find(r.back()) != std::u32string_view::npos) return "v";
    return "n";
}

// The sense of one line, none for a blank or comment line. Returns an error message.
std::string ParseSense(std::string_view line, Families& families, Sense& s, bool& found) {
    found = false;
    if (const std::string_view t = internal::TrimSpace(line); t.empty() || t.starts_with("//")) return {};
    std::vector<std::string_view> fields = internal::Split(line, '\t');
    if (fields.size() < 2 || fields.size() > 5) return std::to_string(fields.size()) + " columns, want 2 to 5";
    fields.resize(5);
    for (std::string_view& f : fields) f = internal::TrimSpace(f);
    const std::string_view jp = fields[0], triggers = fields[2], side = fields[3];
    std::string_view kr = fields[1];
    if (jp.empty() || kr.empty()) return "empty KEY or KR";
    std::string_view pos = fields[4], cls;
    if (const std::size_t colon = pos.find(':'); colon != std::string_view::npos) {
        cls = pos.substr(colon + 1);
        pos = pos.substr(0, colon);
    }
    if (pos.empty()) pos = SensePos(jp);
    if (pos != "n" && pos != "v" && pos != "v1" && pos != "v5" && pos != "adj" && pos != "na") {
        return "POS " + internal::Quote(pos) + ", want n, v, v1, v5, adj or na";
    }
    Side sideValue = Side::Before;
    const bool known = side == "before" || side == "after" || side == "sentence";
    if (side == "after") sideValue = Side::After;
    if (side == "sentence") sideValue = Side::Sentence;
    if (triggers.empty() != side.empty() || (!side.empty() && !known)) {
        return "SIDE " + internal::Quote(side) + ": want before, after or sentence with triggers, empty without";
    }
    bool adjective = kr.ends_with("하다");
    const std::string_view root = adjective ? kr.substr(0, kr.size() - std::string_view("하다").size()) : kr;
    adjective = adjective && pos == "n" && !root.empty();
    if (adjective) kr = root;
    std::vector<internal::Rec> recs;
    if (std::string err = internal::Compile(jp, kr, pos, cls, false, nullptr, families, recs); !err.empty()) return err;
    const internal::Rec& e = recs[0];
    s = Sense();
    s.key = e.key;
    s.classDigit = e.part.size() > 1 ? e.part[1] : 0;
    s.target = e.target;
    s.side = sideValue;
    s.adjective = adjective;
    if (!triggers.empty()) {
        for (std::string_view t : internal::Split(triggers, ',')) {
            t = internal::TrimSpace(t);
            if (t.empty()) return "empty trigger";
            s.triggers.emplace_back(t);
        }
    }
    found = true;
    return {};
}

}  // namespace

CompiledSenses ParseSenses(std::string_view text, Families& families) {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    CompiledSenses out;
    int n = 0;
    for (std::string_view line : internal::Split(text, '\n')) {
        n++;
        while (line.ends_with('\r')) line.remove_suffix(1);
        Sense s;
        bool found = false;
        if (std::string err = ParseSense(line, families, s, found); !err.empty()) {
            out.errors.push_back({n, std::move(err)});
        } else if (found) {
            s.row = n;
            std::string key = s.key;
            out.table[key].push_back(std::move(s));
        }
    }
    return out;
}

}  // namespace hdTrans::userdict
