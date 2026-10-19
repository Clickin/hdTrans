#include <utility>

#include "rulec/rulec.h"

namespace hdTrans::rulec {

namespace {

using rules::NodeKind;
using rules::Op;

constexpr OpInfo kOps[] = {
    {Op::SrcEq, 'S', "==", false, Operand::Surface, false, "src", "=="},
    {Op::SrcNe, 'S', "!=", false, Operand::Surface, false, "src", "!="},
    {Op::SrcEqW, 'S', "==", true, Operand::Surface, false, "src", "=="},
    {Op::DstEq, 'D', "==", false, Operand::Surface, false, "ko", "=="},
    {Op::DstNe, 'D', "!=", false, Operand::Surface, false, "ko", "!="},
    {Op::DstSet, 'D', "=", false, Operand::Text, true, "ko", ":="},
    {Op::DstSetW, 'D', "=", true, Operand::Text, true, "ko", ":="},
    {Op::PumsaEq, 'P', "==", false, Operand::Code3, false, "pos", "is"},
    {Op::PumsaNe, 'P', "!=", false, Operand::Code3, false, "pos", "is not"},
    {Op::AttrEq, 'A', "==", false, Operand::Code3, false, "sem", "has"},
    {Op::AttrNe, 'A', "!=", false, Operand::Code3, false, "sem", "lacks"},
    {Op::AttrMinusEq, 'A', "-=", false, Operand::Code3, false, "sem", "under"},
    {Op::AttrMinusEqW, 'A', "-=", true, Operand::Code3, false, "sem", "under"},
    {Op::GramEq, 'G', "==", false, Operand::Code2, false, "gram", "has"},
    {Op::GramNe, 'G', "!=", false, Operand::Code2, false, "gram", "lacks"},
    {Op::GramMinusEq, 'G', "-=", false, Operand::Code2, false, "gram", "within"},
    {Op::GobiSrcEq, 'U', "==", false, Operand::Surface, false, "end.src", "=="},
    {Op::GobiSrcNe, 'U', "!=", false, Operand::Surface, false, "end.src", "!="},
    {Op::GobiDstEq, 'Y', "==", false, Operand::Surface, false, "end.ko", "=="},
    {Op::GobiDstSet, 'Y', "=", false, Operand::Text, true, "end.ko", ":="},
    {Op::GobiDstSetW, 'Y', "=", true, Operand::Text, true, "end.ko", ":="},
    {Op::GobiPumsaEq, 'M', "==", false, Operand::Code3, false, "end.pos", "is"},
    {Op::GobiAttrEq, 'T', "==", false, Operand::Code3, false, "end.sem", "isa"},
    {Op::GobiAttrMinusEq, 'T', "-=", false, Operand::Code3, false, "end.sem", "under"},
    {Op::GobiGramEq, 'W', "==", false, Operand::Code2, false, "end.gram", "has"},
    {Op::GobiGramNe, 'W', "!=", false, Operand::Code2, false, "end.gram", "lacks"},
};

bool HasMarker(std::string_view v) {
    return v.starts_with("L") || v.starts_with("R") || v.starts_with("11") || v.starts_with("22") ||
           v.starts_with("00") || v.starts_with("99");
}

void LowerExpr(const Expr& e, rules::Program& p) {
    if (e.kind == NodeKind::Atom) {
        rules::Node n{NodeKind::Atom, e.op, e.delta, 0, 0, 0};
        if (Info(e.op).word) {
            n.rightDelta = e.rightDelta;
        } else {
            n.first = static_cast<uint32_t>(p.literals.size());
            n.second = static_cast<uint32_t>(e.literal.size());
            p.literals += e.literal;
        }
        p.nodes.push_back(n);
        return;
    }
    const std::size_t self = p.nodes.size();
    p.nodes.push_back({e.kind, Op::SrcEq, 0, 0, static_cast<uint32_t>(e.children.size()), 0});
    for (const Expr& c : e.children) LowerExpr(c, p);
    p.nodes[self].second = static_cast<uint32_t>(p.nodes.size());
}

void CanonicalExpr(Expr& e) {
    for (Expr& c : e.children) CanonicalExpr(c);
    if (e.kind != NodeKind::Atom || Info(e.op).word || Info(e.op).operand != Operand::Surface) return;
    std::string_view v;
    const Match m = SplitSurface(e.literal, v);
    e.literal = JoinSurface(m, v);
}

int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// A quoted string at text[i]; i ends past it.
bool Quoted(std::string_view text, std::size_t& i, std::string& out) {
    if (i >= text.size() || text[i] != '"') return false;
    for (i++; i < text.size() && text[i] != '"'; i++) {
        if (text[i] != '\\') {
            out += text[i];
        } else if (i + 1 < text.size() && (text[i + 1] == '\\' || text[i + 1] == '"')) {
            out += text[++i];
        } else if (i + 3 < text.size() && text[i + 1] == 'x' && Hex(text[i + 2]) >= 0 && Hex(text[i + 3]) >= 0) {
            out += static_cast<char>(Hex(text[i + 2]) * 16 + Hex(text[i + 3]));
            i += 3;
        } else {
            return false;
        }
    }
    if (i >= text.size()) return false;
    i++;
    return true;
}

void Spaces(std::string_view text, std::size_t& i) {
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) i++;
}

std::string Quote(std::string_view s) {
    std::string out;
    AppendQuoted(out, s);
    return out;
}

}  // namespace

void AppendQuoted(std::string& out, std::string_view s) {
    static const char kHex[] = "0123456789ABCDEF";
    out += '"';
    for (std::size_t i = 0; i < s.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const std::size_t n = c < 0x80 ? 1 : c >> 5 == 6 ? 2 : c >> 4 == 14 ? 3 : c >> 3 == 30 ? 4 : 0;
        bool valid = n > 0 && i + n <= s.size();
        for (std::size_t k = 1; valid && k < n; k++) valid = (static_cast<unsigned char>(s[i + k]) & 0xc0) == 0x80;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (valid && c >= 0x20 && c != 0x7f) {
            out.append(s.substr(i, n));
            i += n - 1;
        } else {
            out += "\\x";
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    out += '"';
}

bool ParseTest(std::string_view text, uint32_t offset, Diagnostics& diags, RuleTest& test) {
    std::size_t i = 0;
    auto fail = [&](const char* message) {
        diags.Error(offset + static_cast<uint32_t>(i), 1, message);
        return false;
    };
    test.offset = offset;
    test.length = static_cast<uint32_t>(text.size());
    Spaces(text, i);
    if (!Quoted(text, i, test.input)) return fail("expected the quoted input of the test");
    Spaces(text, i);
    const std::string_view rest = text.substr(i);
    if (rest.starts_with("->")) {
        i += 2;
        Spaces(text, i);
        if (text.substr(i).starts_with("unchanged")) {
            test.kind = TestKind::Unchanged;
            i += 9;
        } else if (Quoted(text, i, test.expected)) {
            test.kind = TestKind::Text;
        } else {
            return fail("expected the quoted translation or 'unchanged' after '->'");
        }
    } else if (rest.starts_with("picks")) {
        i += 5;
        Spaces(text, i);
        test.kind = TestKind::Picks;
        if (!Quoted(text, i, test.expected)) return fail("expected the quoted target after 'picks'");
    } else if (rest.starts_with("silent")) {
        test.kind = TestKind::Silent;
        i += 6;
    } else {
        return fail("expected '->', 'picks' or 'silent' after the test input");
    }
    Spaces(text, i);
    if (i < text.size() && text[i] != '#') return fail("unexpected text after the test");
    return true;
}

std::string PrintTest(const RuleTest& t) {
    std::string out = Quote(t.input);
    switch (t.kind) {
    case TestKind::Text: return out + " -> " + Quote(t.expected);
    case TestKind::Unchanged: return out + " -> unchanged";
    case TestKind::Picks: return out + " picks " + Quote(t.expected);
    case TestKind::Silent: return out + " silent";
    }
    return out;
}

std::span<const OpInfo> Ops() { return kOps; }

const OpInfo& Info(Op op) {
    for (const OpInfo& o : kOps) {
        if (o.op == op) return o;
    }
    return kOps[0];
}

Match SplitSurface(std::string_view raw, std::string_view& value) {
    value = raw;
    if (raw.starts_with("L")) return value = raw.substr(1), Match::Prefix;
    if (raw.starts_with("R")) return value = raw.substr(1), Match::Suffix;
    if (raw.starts_with("11")) return value = raw.substr(2), Match::Prefix;
    if (raw.starts_with("22")) return value = raw.substr(2), Match::Suffix;
    if (raw.starts_with("00")) return value = raw.substr(2), Match::Inside;
    if (raw.starts_with("99")) return value = raw.substr(2), Match::Exact;
    return Match::Exact;
}

std::string JoinSurface(Match m, std::string_view value) {
    switch (m) {
    case Match::Prefix: return "L" + std::string(value);
    case Match::Suffix: return "R" + std::string(value);
    case Match::Inside: return "00" + std::string(value);
    case Match::Exact: break;
    }
    return HasMarker(value) ? "99" + std::string(value) : std::string(value);
}

void Normalize(Expr& e) {
    if (e.kind == NodeKind::Atom) return;
    std::vector<Expr> kids;
    for (Expr& c : e.children) {
        Normalize(c);
        while (c.kind != NodeKind::Atom && c.children.size() == 1) {
            Expr only = std::move(c.children[0]);
            c = std::move(only);
        }
        if (c.kind == e.kind) {
            for (Expr& g : c.children) kids.push_back(std::move(g));
        } else {
            kids.push_back(std::move(c));
        }
    }
    e.children = std::move(kids);
}

void Normalize(File& file) {
    for (auto* rules : {&file.local, &file.global}) {
        for (Rule& r : *rules) Normalize(r.body);
    }
}

void CanonicalLiterals(File& file) {
    for (auto* rules : {&file.local, &file.global}) {
        for (Rule& r : *rules) CanonicalExpr(r.body);
    }
}

bool SameTree(const Expr& a, const Expr& b) {
    if (a.kind != b.kind || a.children.size() != b.children.size()) return false;
    if (a.kind == NodeKind::Atom) {
        if (a.op != b.op || a.delta != b.delta) return false;
        return Info(a.op).word ? a.rightDelta == b.rightDelta : a.literal == b.literal;
    }
    for (std::size_t i = 0; i < a.children.size(); i++) {
        if (!SameTree(a.children[i], b.children[i])) return false;
    }
    return true;
}

namespace {

void DumpNode(const rules::Program& p, uint32_t i, std::string& out) {
    const rules::Node& n = p.nodes[i];
    if (n.kind != NodeKind::Atom) {
        out += n.kind == NodeKind::Or ? "||" : n.kind == NodeKind::And ? "&&" : "&";
        out += std::to_string(n.first);
        uint32_t child = i + 1;
        for (uint32_t k = 0; k < n.first; k++) {
            out += ' ';
            DumpNode(p, child, out);
            child = p.nodes[child].kind == NodeKind::Atom ? child + 1 : p.nodes[child].second;
        }
        return;
    }
    const OpInfo& o = Info(n.op);
    auto delta = [](int d) { return (d < 0 ? "-" : "+") + std::to_string(d < 0 ? -d : d); };
    out += o.field;
    out += delta(n.delta);
    out += o.flat;
    out += ' ';
    if (o.word) {
        out += "@" + delta(n.rightDelta);
    } else {
        AppendQuoted(out, std::string_view(p.literals).substr(n.first, n.second));
    }
}

}  // namespace

std::string Dump(const rules::Program& p) {
    std::string out;
    for (std::size_t i = 0; i < p.localRules.size(); i++) {
        out += "L " + std::to_string(i) + " ";
        DumpNode(p, p.localRules[i], out);
        out += '\n';
    }
    for (std::size_t i = 0; i < p.globalRules.size(); i++) {
        const rules::GlobalRule& g = p.globalRules[i];
        out += "G " + std::to_string(i) + " ";
        out += static_cast<char>(g.pumsa);
        out += " " + std::to_string(g.number) + (g.uganRule ? " 1 " : " 0 ");
        DumpNode(p, g.root, out);
        out += '\n';
    }
    return out;
}

rules::Program Lower(const File& file) {
    rules::Program p;
    for (const Rule& r : file.local) {
        p.localRules.push_back(static_cast<uint32_t>(p.nodes.size()));
        LowerExpr(r.body, p);
    }
    for (const Rule& r : file.global) {
        p.globalRules.push_back({r.pumsa, r.number, r.ugan, static_cast<uint32_t>(p.nodes.size()), r.index});
        LowerExpr(r.body, p);
    }
    return p;
}

}  // namespace hdTrans::rulec
