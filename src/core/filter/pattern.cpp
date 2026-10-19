#include "core/filter/pattern.h"

#include <vector>

namespace hdTrans::filter {

namespace {

constexpr int kMaxCount = 1000;

bool AsciiAlnum(wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
bool Digit(wchar_t c) { return c >= L'0' && c <= L'9'; }

int HexValue(wchar_t c) {
    if (Digit(c)) return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

class Checker {
public:
    explicit Checker(std::wstring_view p) : p_(p) {}

    bool Run(std::wstring& literal, std::string& error) {
        bool unbounded = false;
        if (!Disjunction(0, unbounded)) {
            error = error_;
            return false;
        }
        if (i_ < p_.size()) {
            error = "unbalanced )";
            return false;
        }
        EndRun();
        literal = alternatives_ ? std::wstring() : best_;
        return true;
    }

private:
    bool Fail(const char* why) {
        error_ = why;
        return false;
    }
    bool At(wchar_t c) const { return i_ < p_.size() && p_[i_] == c; }
    bool Quantifier() const { return At(L'*') || At(L'+') || At(L'?') || At(L'{'); }

    // The literal runs of the top level: characters that every match holds in a row.
    void EndRun() {
        if (run_.size() > best_.size()) best_ = run_;
        run_.clear();
    }

    bool Disjunction(int depth, bool& unbounded) {
        for (;;) {
            if (!Alternative(depth, unbounded)) return false;
            if (!At(L'|')) return true;
            i_++;
            if (depth == 0) alternatives_ = true;
        }
    }

    bool Alternative(int depth, bool& unbounded) {
        while (i_ < p_.size() && !At(L'|') && !At(L')')) {
            bool assertion = false;
            if (!Term(depth, unbounded, assertion)) return false;
            if (assertion && Quantifier()) return Fail("a quantifier after an assertion");
        }
        return true;
    }

    bool Term(int depth, bool& unbounded, bool& assertion) {
        const wchar_t c = p_[i_];
        if (c == L'^' || c == L'$' || (c == L'\\' && i_ + 1 < p_.size() && (p_[i_ + 1] == L'b' || p_[i_ + 1] == L'B'))) {
            i_ += c == L'\\' ? 2 : 1;
            assertion = true;
            if (depth == 0) EndRun();
            return true;
        }
        if (c == L'(' && i_ + 1 < p_.size() && p_[i_ + 1] == L'?') {
            if (i_ + 2 >= p_.size()) return Fail("unterminated group");
            const wchar_t k = p_[i_ + 2];
            if (k == L'<') return Fail("lookbehind is not supported");
            if (k == L'=' || k == L'!') {
                i_ += 3;
                lookahead_++;
                bool inner = false;
                if (!Disjunction(depth + 1, inner)) return false;
                lookahead_--;
                if (!At(L')')) return Fail("unterminated group");
                i_++;
                unbounded = unbounded || inner;
                assertion = true;
                if (depth == 0) EndRun();
                return true;
            }
            if (k != L':') return Fail("unsupported group");
        }
        bool inner = false;
        wchar_t ch = 0;
        if (!Atom(depth, inner, ch)) return false;
        if (!Quantifier()) {
            if (depth == 0) {
                if (ch != 0) run_ += ch;
                else EndRun();
            }
            unbounded = unbounded || inner;
            return true;
        }
        int min = 0;
        bool open = false;
        if (!ReadQuantifier(min, open)) return false;
        if (open && inner) return Fail("nested unbounded repetition");
        if (depth == 0) {
            if (ch != 0 && min > 0) run_ += ch;
            EndRun();
            if (ch != 0 && min > 0) run_ += ch;
        }
        unbounded = unbounded || inner || open;
        return true;
    }

    // An atom; ch is its character when it matches exactly one fixed character, else 0.
    bool Atom(int depth, bool& unbounded, wchar_t& ch) {
        const wchar_t c = p_[i_];
        switch (c) {
        case L'.':
            i_++;
            return true;
        case L'[':
            return Class();
        case L'(': {
            const bool capture = !(i_ + 2 < p_.size() && p_[i_ + 1] == L'?' && p_[i_ + 2] == L':');
            i_ += capture ? 1 : 3;
            std::size_t number = 0;
            if (capture) {
                closed_.push_back(false);
                number = closed_.size() - 1;
            }
            if (!Disjunction(depth + 1, unbounded)) return false;
            if (!At(L')')) return Fail("unterminated group");
            i_++;
            if (capture) closed_[number] = true;
            return true;
        }
        case L'*': case L'+': case L'?': case L'{':
            return Fail("nothing to repeat");
        case L']': case L'}':
            return Fail("unmatched ] or }");
        case L'\\':
            return Escape(false, ch);
        default:
            ch = c;
            i_++;
            return true;
        }
    }

    bool ReadCount(int& n) {
        if (i_ >= p_.size() || !Digit(p_[i_])) return Fail("malformed {}");
        n = 0;
        while (i_ < p_.size() && Digit(p_[i_])) {
            n = n * 10 + (p_[i_++] - L'0');
            if (n > kMaxCount) return Fail("a count above 1000");
        }
        return true;
    }

    bool ReadQuantifier(int& min, bool& open) {
        const wchar_t q = p_[i_++];
        if (q == L'*' || q == L'+' || q == L'?') {
            min = q == L'+' ? 1 : 0;
            open = q != L'?';
        } else {
            if (!ReadCount(min)) return false;
            int max = min;
            if (At(L',')) {
                i_++;
                open = At(L'}');
                if (!open && !ReadCount(max)) return false;
            }
            if (!At(L'}')) return Fail("malformed {}");
            i_++;
            if (!open && max < min) return Fail("a {} count out of order");
        }
        if (At(L'?')) i_++;
        if (Quantifier()) return Fail("stacked quantifiers");
        return true;
    }

    // An escape after "\": the character it stands for in ch, or 0 for a class escape
    // or a back-reference.
    bool Escape(bool inClass, wchar_t& ch) {
        i_++;
        if (i_ >= p_.size()) return Fail("\\ at the end");
        const wchar_t e = p_[i_++];
        ch = 0;
        switch (e) {
        case L'd': case L'D': case L's': case L'S': case L'w': case L'W':
            return true;
        case L'f': ch = L'\f'; return true;
        case L'n': ch = L'\n'; return true;
        case L'r': ch = L'\r'; return true;
        case L't': ch = L'\t'; return true;
        case L'v': ch = L'\v'; return true;
        case L'b': ch = L'\b'; return true;  // in a class; outside one it is an assertion
        case L'0':
            if (i_ < p_.size() && Digit(p_[i_])) return Fail("\\0 followed by a digit");
            ch = L'\0';
            return true;
        case L'c':
            if (i_ >= p_.size() || !((p_[i_] >= L'a' && p_[i_] <= L'z') || (p_[i_] >= L'A' && p_[i_] <= L'Z'))) {
                return Fail("malformed \\c");
            }
            ch = static_cast<wchar_t>(p_[i_++] % 32);
            return true;
        case L'x': case L'u': {
            const int digits = e == L'x' ? 2 : 4;
            unsigned v = 0;
            for (int k = 0; k < digits; k++) {
                const int h = i_ < p_.size() ? HexValue(p_[i_]) : -1;
                if (h < 0) return Fail("malformed hexadecimal escape");
                v = v * 16 + static_cast<unsigned>(h);
                i_++;
            }
            if (v == 0) return Fail("an escape of NUL");
            ch = static_cast<wchar_t>(v);
            return true;
        }
        }
        if (Digit(e)) {
            if (inClass) return Fail("a back-reference in a class");
            if (lookahead_ > 0) return Fail("a back-reference in a lookahead");
            std::size_t n = static_cast<std::size_t>(e - L'0');
            while (i_ < p_.size() && Digit(p_[i_])) {
                n = n * 10 + static_cast<std::size_t>(p_[i_++] - L'0');
                if (n > closed_.size()) return Fail("a back-reference to a group not closed before it");
            }
            if (n > closed_.size() || !closed_[n - 1]) return Fail("a back-reference to a group not closed before it");
            return true;
        }
        if (AsciiAlnum(e) || e == L'_') return Fail("an escape that ECMAScript does not define");
        if (e > 0x7F) return Fail("an escape of a non-ASCII character");
        ch = e;
        return true;
    }

    // One character of a class, or a class escape (ch 0, single false).
    bool ClassAtom(wchar_t& ch, bool& single) {
        const wchar_t c = p_[i_];
        if (c == L'[' && i_ + 1 < p_.size() && (p_[i_ + 1] == L':' || p_[i_ + 1] == L'.' || p_[i_ + 1] == L'=')) {
            return Fail("[: [. or [= in a class");
        }
        if (c == L'\\') {
            const wchar_t e = i_ + 1 < p_.size() ? p_[i_ + 1] : 0;
            if (!Escape(true, ch)) return false;
            single = !(e == L'd' || e == L'D' || e == L's' || e == L'S' || e == L'w' || e == L'W');
            return true;
        }
        ch = c;
        single = true;
        i_++;
        return true;
    }

    bool Class() {
        i_++;
        if (At(L'^')) i_++;
        if (At(L']')) return Fail("an empty class");
        for (;;) {
            if (i_ >= p_.size()) return Fail("unterminated class");
            if (At(L']')) {
                i_++;
                return true;
            }
            wchar_t lo = 0;
            bool single = false;
            if (!ClassAtom(lo, single)) return false;
            if (At(L'-') && i_ + 1 < p_.size() && p_[i_ + 1] != L']') {
                i_++;
                wchar_t hi = 0;
                bool hiSingle = false;
                if (!ClassAtom(hi, hiSingle)) return false;
                if (!single || !hiSingle) return Fail("a range bounded by a class");
                if (hi < lo) return Fail("a range out of order");
            }
        }
    }

    std::wstring_view p_;
    std::size_t i_ = 0;
    std::string error_;
    int lookahead_ = 0;
    std::vector<bool> closed_;  // per capturing group in order of opening: whether it is closed
    bool alternatives_ = false;
    std::wstring run_, best_;
};

}  // namespace

bool CheckPattern(std::wstring_view p, std::wstring& literal, std::string& error) {
    return Checker(p).Run(literal, error);
}

}  // namespace hdTrans::filter
