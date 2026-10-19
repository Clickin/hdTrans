// The readable syntax:
//
//   file      = { line }
//   line      = header | statement | (blank or "#" comment)
//   header    = "local" INT | "global" INT ("stem" | "ending") "pos" KEY
//   statement = or NEWLINE            (newlines inside parentheses are spaces)
//   or        = and { "or" and }
//   and       = primary { "and" primary }
//   primary   = "(" or ")" | "all" "(" or { "," or } ")" | "true" | "false" | atom
//   atom      = field test operand
//   field     = ["end" "."] NAME ["[" DELTA "]"]
//   test      = "==" | "!=" | ":=" | ["not"] ("starts" | "ends" | "inside")
//             | "is" ["not"] | "has" | "lacks" | "under" | "within" | "isa"
//   operand   = STRING | field | CODE { "|" CODE }
#include <cctype>

#include "rulec/rulec.h"

namespace hdTrans::rulec {

namespace {

using rules::NodeKind;

enum class Tok : uint8_t { End, Newline, Ident, Int, String, Code, Punct };

struct Token {
    Tok kind = Tok::End;
    std::string_view text;
    uint32_t offset = 0;
    std::string value;  // String: the decoded bytes
};

bool CodeByte(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '*' || c == ':' || c == '?';
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

class Parser {
public:
    Parser(const Source& source, Diagnostics& diags) : s_(source.text), diags_(diags) {}

    File Run() {
        File file;
        Advance();
        Rule* rule = nullptr;
        while (tok_.kind != Tok::End) {
            if (tok_.kind == Tok::Newline) {
                Advance();
                continue;
            }
            const std::size_t errors = diags_.list.size();
            if (tok_.kind == Tok::Ident && tok_.text == "test") {
                const std::size_t start = tok_.offset + 4;
                std::size_t eol = s_.find('\n', start);
                if (eol == std::string::npos) eol = s_.size();
                std::size_t end = eol;
                if (end > start && s_[end - 1] == '\r') end--;
                RuleTest t;
                if (rule == nullptr) {
                    Error(tok_, "test line before the first rule header");
                } else if (ParseTest(std::string_view(s_).substr(start, end - start), static_cast<uint32_t>(start), diags_, t)) {
                    t.offset = tok_.offset;
                    t.length = static_cast<uint32_t>(end - tok_.offset);
                    rule->tests.push_back(std::move(t));
                }
                pos_ = eol;
                Advance();
                continue;
            }
            if (tok_.kind == Tok::Ident && (tok_.text == "local" || tok_.text == "global")) {
                rule = Header(file);
            } else if (rule == nullptr) {
                Error(tok_, "statement before the first rule header");
            } else {
                Expr e;
                if (Or(e)) {
                    rule->body.children.push_back(std::move(e));
                    if (tok_.kind != Tok::Newline && tok_.kind != Tok::End)
                        Error(tok_, "expected 'and', 'or' or the end of the statement");
                }
            }
            if (diags_.list.size() != errors) SkipLine();
        }
        return file;
    }

private:
    // Lexing. Newlines are tokens only outside parentheses.
    void Advance() {
        for (;;) {
            while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' ||
                                        (s_[pos_] == '\n' && depth_ > 0)))
                pos_++;
            if (pos_ < s_.size() && s_[pos_] == '#') {
                while (pos_ < s_.size() && s_[pos_] != '\n') pos_++;
                continue;
            }
            break;
        }
        tok_ = Token{};
        tok_.offset = static_cast<uint32_t>(pos_);
        if (pos_ >= s_.size()) return;
        const std::size_t start = pos_;
        const char c = s_[pos_];
        auto finish = [&](Tok kind) {
            tok_.kind = kind;
            tok_.text = std::string_view(s_).substr(start, pos_ - start);
        };
        if (c == '\n') {
            pos_++;
            return finish(Tok::Newline);
        }
        if (c == '"') {
            pos_++;
            while (pos_ < s_.size() && s_[pos_] != '"' && s_[pos_] != '\n') {
                if (s_[pos_] != '\\') {
                    tok_.value += s_[pos_++];
                    continue;
                }
                const char e = pos_ + 1 < s_.size() ? s_[pos_ + 1] : 0;
                if (e == '\\' || e == '"') {
                    tok_.value += e;
                    pos_ += 2;
                } else if (e == 'x' && pos_ + 3 < s_.size() && HexDigit(s_[pos_ + 2]) >= 0 && HexDigit(s_[pos_ + 3]) >= 0) {
                    tok_.value += static_cast<char>(HexDigit(s_[pos_ + 2]) * 16 + HexDigit(s_[pos_ + 3]));
                    pos_ += 4;
                } else {
                    diags_.Error(static_cast<uint32_t>(pos_), 2, "bad escape: expected \\\\, \\\" or \\xHH");
                    pos_ += 1;
                }
            }
            if (pos_ >= s_.size() || s_[pos_] != '"') {
                diags_.Error(static_cast<uint32_t>(start), static_cast<uint32_t>(pos_ - start), "unterminated string literal");
            } else {
                pos_++;
            }
            return finish(Tok::String);
        }
        if (std::islower(static_cast<unsigned char>(c))) {
            while (pos_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_')) pos_++;
            return finish(Tok::Ident);
        }
        if ((c == '+' || c == '-') && pos_ + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_ + 1]))) pos_++;
        if (std::isdigit(static_cast<unsigned char>(s_[pos_])) && !codes_) {
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) pos_++;
            return finish(Tok::Int);
        }
        if (codes_ && CodeByte(s_[pos_])) {
            while (pos_ < s_.size() && CodeByte(s_[pos_])) pos_++;
            return finish(Tok::Code);
        }
        pos_ = start;
        for (const char* p : {"==", "!=", ":="}) {
            if (std::string_view(s_).substr(pos_, 2) == p) {
                pos_ += 2;
                return finish(Tok::Punct);
            }
        }
        if (c == '(') depth_++;
        if (c == ')' && depth_ > 0) depth_--;
        pos_++;
        while (pos_ < s_.size() && (static_cast<unsigned char>(s_[pos_]) & 0xc0) == 0x80) pos_++;
        finish(Tok::Punct);
    }

    bool Is(std::string_view text) const { return (tok_.kind == Tok::Ident || tok_.kind == Tok::Punct) && tok_.text == text; }

    bool Accept(std::string_view text) {
        if (!Is(text)) return false;
        Advance();
        return true;
    }

    bool Expect(std::string_view text, const char* where) {
        if (Accept(text)) return true;
        return Error(tok_, "expected '" + std::string(text) + "' " + where);
    }

    bool Error(const Token& t, std::string message) {
        std::string found = t.kind == Tok::End ? "the end of the file" : t.kind == Tok::Newline ? "the end of the line"
                                                                                               : "'" + std::string(t.text) + "'";
        diags_.Error(t.offset, static_cast<uint32_t>(t.text.empty() ? 1 : t.text.size()), message + ", found " + found);
        return false;
    }

    void SkipLine() {
        depth_ = 0;
        while (tok_.kind != Tok::Newline && tok_.kind != Tok::End) Advance();
    }

    bool Int(int& out, const char* what) {
        if (tok_.kind != Tok::Int || tok_.text.size() > 10) return Error(tok_, std::string("expected ") + what);
        long long v = 0;
        std::size_t i = tok_.text[0] == '+' || tok_.text[0] == '-' ? 1 : 0;
        for (; i < tok_.text.size(); i++) v = v * 10 + (tok_.text[i] - '0');
        if (tok_.text[0] == '-') v = -v;
        if (v < -(1LL << 30) || v > 1LL << 30) return Error(tok_, std::string("expected ") + what + " within 2^30");
        out = static_cast<int>(v);
        Advance();
        return true;
    }

    Rule* Header(File& file) {
        const bool local = tok_.text == "local";
        const Token head = tok_;
        Advance();
        Rule r;
        r.kind = local ? 'L' : 'G';
        r.offset = head.offset;
        r.body.kind = NodeKind::Or;
        r.body.offset = head.offset;
        r.body.length = static_cast<uint32_t>(head.text.size());
        std::vector<Rule>& list = local ? file.local : file.global;
        r.index = static_cast<int>(list.size());
        if (local) {
            if (!file.global.empty()) return Error(head, "local rule after a global rule"), nullptr;
            const Token at = tok_;
            int n = 0;
            if (!Int(n, "the local rule index")) return nullptr;
            if (n != r.index) return Error(at, "expected local rule " + std::to_string(r.index)), nullptr;
        } else {
            if (!Int(r.number, "the global rule number")) return nullptr;
            if (Accept("stem")) {
                r.ugan = true;
            } else if (!Accept("ending")) {
                return Error(tok_, "expected 'stem' or 'ending'"), nullptr;
            }
            if (!Expect("pos", "before the pumsa key")) return nullptr;
            if (tok_.kind == Tok::Newline || tok_.kind == Tok::End || tok_.text.size() != 1)
                return Error(tok_, "expected a one-byte pumsa key"), nullptr;
            r.pumsa = static_cast<unsigned char>(tok_.text[0]);
            Advance();
        }
        if (tok_.kind != Tok::Newline && tok_.kind != Tok::End) return Error(tok_, "expected the end of the header"), nullptr;
        r.length = tok_.offset - head.offset;
        list.push_back(std::move(r));
        return &list.back();
    }

    bool Or(Expr& e) {
        Expr first;
        if (!And(first)) return false;
        if (!Is("or")) return e = std::move(first), true;
        e = Expr{};
        e.kind = NodeKind::Or;
        e.offset = first.offset;
        e.children.push_back(std::move(first));
        while (Is("or")) {
            Advance();
            e.children.emplace_back();
            if (!And(e.children.back())) return false;
        }
        e.length = PreviousEnd() - e.offset;
        return true;
    }

    bool And(Expr& e) {
        Expr first;
        if (!Primary(first)) return false;
        if (!Is("and")) return e = std::move(first), true;
        e = Expr{};
        e.kind = NodeKind::And;
        e.offset = first.offset;
        e.children.push_back(std::move(first));
        while (Is("and")) {
            Advance();
            e.children.emplace_back();
            if (!Primary(e.children.back())) return false;
        }
        e.length = PreviousEnd() - e.offset;
        return true;
    }

    bool Primary(Expr& e) {
        const Token t = tok_;
        if (Accept("(")) {
            if (!Or(e)) return false;
            return Expect(")", "to close the '('");
        }
        if (Is("all")) {
            Advance();
            e.kind = NodeKind::AndAll;
            e.offset = t.offset;
            e.length = 3;
            if (!Expect("(", "after 'all'")) return false;
            if (Accept(")")) return true;
            do {
                e.children.emplace_back();
                if (!Or(e.children.back())) return false;
            } while (Accept(","));
            if (!Expect(")", "to close 'all('")) return false;
            e.length = PreviousEnd() - e.offset;
            return true;
        }
        if (Is("true") || Is("false")) {
            e.kind = t.text == "true" ? NodeKind::And : NodeKind::Or;
            e.offset = t.offset;
            e.length = static_cast<uint32_t>(t.text.size());
            Advance();
            return true;
        }
        return Atom(e);
    }

    // ["end" "."] NAME ["[" DELTA "]"]
    bool Field(std::string& name, int8_t& delta) {
        if (tok_.kind != Tok::Ident) return Error(tok_, "expected a field (src, ko, pos, sem, gram, end.src, ...)");
        name = std::string(tok_.text);
        Advance();
        if (name == "end") {
            if (!Expect(".", "after 'end'")) return false;
            if (tok_.kind != Tok::Ident) return Error(tok_, "expected an ending field after 'end.'");
            name += "." + std::string(tok_.text);
            Advance();
        }
        bool known = false;
        for (const OpInfo& o : Ops()) known = known || name == o.name;
        if (!known) return Error(tok_, "unknown field '" + name + "'");
        delta = 0;
        if (Accept("[")) {
            int d = 0;
            const Token at = tok_;
            if (!Int(d, "a delta")) return false;
            if (d < -127 || d > 127) return Error(at, "delta out of -127..127");
            delta = static_cast<int8_t>(d);
            return Expect("]", "after the delta");
        }
        return true;
    }

    bool Atom(Expr& e) {
        const Token start = tok_;
        e.kind = NodeKind::Atom;
        e.offset = start.offset;
        std::string name;
        if (!Field(name, e.delta)) return false;
        const Token testToken = tok_;
        std::string test;
        Match match = Match::Exact;
        const bool negated = Accept("not");
        if (negated || Is("starts") || Is("ends") || Is("inside")) {
            test = negated ? "!=" : "==";
            if (Is("starts")) match = Match::Prefix;
            else if (Is("ends")) match = Match::Suffix;
            else if (Is("inside")) match = Match::Inside;
            else return Error(tok_, "expected 'starts', 'ends' or 'inside' after 'not'");
            Advance();
        } else if (Is("==") || Is("!=") || Is(":=") || Is("has") || Is("lacks") || Is("under") || Is("within") ||
                   Is("isa")) {
            test = std::string(tok_.text);
            Advance();
        } else if (Accept("is")) {
            test = Accept("not") ? "is not" : "is";
        } else {
            return Error(tok_, "expected a test (==, !=, :=, starts, ends, inside, is, has, lacks, under, within, isa)");
        }
        const OpInfo* candidates[2] = {nullptr, nullptr};
        for (const OpInfo& o : Ops()) {
            if (name == o.name && test == o.keyword) candidates[o.word ? 1 : 0] = &o;
        }
        if (candidates[0] == nullptr && candidates[1] == nullptr) {
            std::string known;
            for (const OpInfo& o : Ops()) {
                if (name == o.name && known.find(o.keyword) == std::string::npos)
                    known += std::string(known.empty() ? "" : ", ") + o.keyword;
            }
            return Error(testToken, "field " + name + " has no test '" + test + "'; it has " + known);
        }
        const OpInfo* any = candidates[0] ? candidates[0] : candidates[1];
        const bool codeOperand = any->operand == Operand::Code2 || any->operand == Operand::Code3;
        const Token operand = tok_;
        if (tok_.kind == Tok::Ident) {
            if (candidates[1] == nullptr) return Error(tok_, "field " + name + " '" + test + "' takes no word operand");
            std::string right;
            if (!Field(right, e.rightDelta)) return false;
            if (right != name) return Error(operand, "the word operand must be the field " + name + " of another word");
            e.op = candidates[1]->op;
        } else {
            if (candidates[0] == nullptr) return Error(tok_, "field " + name + " '" + test + "' takes a word operand");
            e.op = candidates[0]->op;
            if (tok_.kind == Tok::String) {
                e.literal = any->operand == Operand::Surface ? JoinSurface(match, tok_.value) : tok_.value;
                Advance();
            } else if (codeOperand && match == Match::Exact) {
                if (!Codes(e.literal, any->operand == Operand::Code3 ? 3u : 2u)) return false;
            } else {
                return Error(tok_, codeOperand ? "expected codes or a string" : "expected a string");
            }
            if (match != Match::Exact && any->operand != Operand::Surface)
                return Error(testToken, "'" + std::string(testToken.text) + "' needs a surface field");
        }
        e.length = PreviousEnd() - start.offset;
        return true;
    }

    // CODE { "|" CODE }, each code but the last exactly width bytes. The lexer reads the
    // first code from the current position.
    bool Codes(std::string& out, std::size_t width) {
        for (;;) {
            pos_ = tok_.offset;
            codes_ = true;
            Advance();
            codes_ = false;
            if (tok_.kind != Tok::Code) return Error(tok_, "expected a code");
            const Token code = tok_;
            out += code.text;
            Advance();
            const bool more = Is("|");
            if (code.text.size() > width || (more && code.text.size() != width))
                return Error(code, "code is not " + std::to_string(width) + " characters");
            if (!more) return true;
            Advance();
        }
    }

    uint32_t PreviousEnd() const {
        std::size_t e = tok_.offset;
        while (e > 0 && (s_[e - 1] == ' ' || s_[e - 1] == '\t' || s_[e - 1] == '\r' || s_[e - 1] == '\n')) e--;
        return static_cast<uint32_t>(e);
    }

    const std::string& s_;
    Diagnostics& diags_;
    std::size_t pos_ = 0;
    int depth_ = 0;
    bool codes_ = false;
    Token tok_;
};

std::string Delta(int d) { return d == 0 ? "" : "[" + std::string(d < 0 ? "-" : "+") + std::to_string(d < 0 ? -d : d) + "]"; }

void PrintString(std::string& out, std::string_view s) { AppendQuoted(out, s); }

void PrintCodes(std::string& out, std::string_view raw, std::size_t width) {
    bool bare = !raw.empty();
    for (std::size_t i = 0; i < raw.size(); i++) {
        bare = bare && CodeByte(raw[i]) && (i % width != 0 || !std::islower(static_cast<unsigned char>(raw[i])));
    }
    if (!bare) return PrintString(out, raw);
    for (std::size_t i = 0; i < raw.size(); i += width) {
        if (i > 0) out += '|';
        out += raw.substr(i, width);
    }
}

void PrintAtom(std::string& out, const Expr& e) {
    const OpInfo& o = Info(e.op);
    out += o.name;
    out += Delta(e.delta);
    out += ' ';
    if (o.word) {
        out += o.keyword;
        out += ' ';
        out += o.name;
        out += Delta(e.rightDelta);
        return;
    }
    if (o.operand == Operand::Surface) {
        std::string_view v;
        const Match m = SplitSurface(e.literal, v);
        const bool ne = o.flat == std::string_view("!=");
        if (m == Match::Exact) {
            out += ne ? "!= " : "== ";
        } else {
            if (ne) out += "not ";
            out += m == Match::Prefix ? "starts " : m == Match::Suffix ? "ends " : "inside ";
        }
        return PrintString(out, v);
    }
    out += o.keyword;
    out += ' ';
    if (o.operand == Operand::Text) return PrintString(out, e.literal);
    PrintCodes(out, e.literal, o.operand == Operand::Code3 ? 3 : 2);
}

void PrintExpr(std::string& out, const Expr& e, NodeKind parent) {
    if (e.kind == NodeKind::Atom) return PrintAtom(out, e);
    if (e.kind == NodeKind::AndAll) {
        out += "all(";
        for (std::size_t i = 0; i < e.children.size(); i++) {
            if (i > 0) out += ", ";
            PrintExpr(out, e.children[i], NodeKind::AndAll);
        }
        out += ')';
        return;
    }
    if (e.children.empty()) {
        out += e.kind == NodeKind::And ? "true" : "false";
        return;
    }
    const bool parens = e.kind == parent || (parent == NodeKind::And && e.kind == NodeKind::Or);
    if (parens) out += '(';
    for (std::size_t i = 0; i < e.children.size(); i++) {
        if (i > 0) out += e.kind == NodeKind::Or ? " or " : " and ";
        PrintExpr(out, e.children[i], e.kind);
    }
    if (parens) out += ')';
}

void PrintRule(std::string& out, const Rule& r) {
    if (r.kind == 'L') {
        out += "local " + std::to_string(r.index) + "\n";
    } else {
        out += "global " + std::to_string(r.number) + (r.ugan ? " stem pos " : " ending pos ");
        out += static_cast<char>(r.pumsa);
        out += '\n';
    }
    for (const Expr& s : r.body.children) {
        out += "    ";
        PrintExpr(out, s, NodeKind::Or);
        out += '\n';
    }
    for (const RuleTest& t : r.tests) out += "    test " + PrintTest(t) + "\n";
}

}  // namespace

File ParseReadable(const Source& source, Diagnostics& diags) { return Parser(source, diags).Run(); }

std::string PrintReadable(const File& file) {
    std::string out;
    for (const Rule& r : file.local) PrintRule(out, r);
    for (const Rule& r : file.global) PrintRule(out, r);
    return out;
}

}  // namespace hdTrans::rulec
