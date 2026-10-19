#pragma once
// A scripted Item for rule tests. The word at delta d from the anchor exists for d
// in [lo, hi]; its GetCurrentCompDI returns 10+d and GetCurrentCompUmiDI 20+d, and
// every Check and Assign method returns the next answer of the script (false, and
// appended to it, once the script is used up).
//
// Every call is appended to log as: the method id (one byte, kMethods order), the
// receiver's delta (int8), the arguments in order (int as int32 little-endian,
// string as uint32 little-endian length and bytes, word as its delta, 0x7f for
// none), the result (bool as one byte, int as int32, word as its delta). With
// tracing set it is also appended to trace as "Method@d(args)=result ".
//
// Paths enumerates every evaluation path of a rule: starting from the all-false
// script, the last false answer used is flipped to true (dropping the answers
// after it) until all used answers are true. Its hash is FNV-1a 64 over the logs
// of all paths in that order, each followed by 0xff and the rule's result byte.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "core/rules/rules.h"

namespace rulestest {

using hdTrans::rules::Item;

inline constexpr const char* kMethods[] = {
    "GetObject", "GetCurrentCompDI", "GetCurrentCompUmiDI",
    "CheckSrc", "CheckLLSrc", "CheckDst", "AssignDst", "AssignLLDst",
    "CheckPumsa", "CheckAttr", "MinAttrOperation", "CheckLLAttr",
    "CheckGAttr", "MinGAttrOperation",
    "CheckGobiSrc", "CheckGobiDst", "AssignGobiDst", "AssignLLGobiDst",
    "CheckGobiPumsa", "CheckGobiAttr", "MinGobiAttrOperation", "CheckGobiGAttr",
};

class Scripted {
public:
    std::vector<bool> script;
    std::size_t used = 0;
    std::string log;
    std::string trace;
    bool tracing = false;
    int lo = -127, hi = 127;

    Scripted() {
        for (int i = 0; i < 255; i++) words_[i].Bind(this, i - 127);
    }
    Scripted(const Scripted&) = delete;
    Scripted& operator=(const Scripted&) = delete;

    Item* Anchor() { return &words_[127]; }

    void Restart() {
        used = 0;
        log.clear();
        trace.clear();
    }

private:
    class Word final : public Item {
    public:
        void Bind(Scripted* s, int delta) { s_ = s, delta_ = delta; }

        Item* GetObject(int d) override {
            const int t = delta_ + d;
            Item* w = t >= s_->lo && t <= s_->hi && t >= -127 && t <= 127 ? &s_->words_[t + 127] : nullptr;
            Begin(0), Int(d), Result(w);
            return w;
        }
        int GetCurrentCompDI() override { return Begin(1), Result(10 + delta_); }
        int GetCurrentCompUmiDI() override { return Begin(2), Result(20 + delta_); }

        bool CheckSrc(std::string_view s) override { return Begin(3), Str(s), Answer(); }
        bool CheckLLSrc(Item* wi) override { return Begin(4), WordArg(wi), Answer(); }
        bool CheckDst(std::string_view s, int di) override { return Begin(5), Str(s), Int(di), Answer(); }
        bool AssignDst(int di, std::string_view s) override { return Begin(6), Int(di), Str(s), Answer(); }
        bool AssignLLDst(Item* wi, int di) override { return Begin(7), WordArg(wi), Int(di), Answer(); }
        bool CheckPumsa(std::string_view s, int di) override { return Begin(8), Str(s), Int(di), Answer(); }
        bool CheckAttr(std::string_view s, int di) override { return Begin(9), Str(s), Int(di), Answer(); }
        bool MinAttrOperation(int di, std::string_view s) override { return Begin(10), Int(di), Str(s), Answer(); }
        bool CheckLLAttr(int op, Item* wi, int di) override {
            return Begin(11), Int(op), WordArg(wi), Int(di), Answer();
        }
        bool CheckGAttr(std::string_view s, int di) override { return Begin(12), Str(s), Int(di), Answer(); }
        bool MinGAttrOperation(int di, std::string_view s) override { return Begin(13), Int(di), Str(s), Answer(); }
        bool CheckGobiSrc(std::string_view s) override { return Begin(14), Str(s), Answer(); }
        bool CheckGobiDst(std::string_view s, int di) override { return Begin(15), Str(s), Int(di), Answer(); }
        bool AssignGobiDst(int di, std::string_view s) override { return Begin(16), Int(di), Str(s), Answer(); }
        bool AssignLLGobiDst(Item* wi, int di) override { return Begin(17), WordArg(wi), Int(di), Answer(); }
        bool CheckGobiPumsa(std::string_view s, int di) override { return Begin(18), Str(s), Int(di), Answer(); }
        bool CheckGobiAttr(std::string_view s, int di) override { return Begin(19), Str(s), Int(di), Answer(); }
        bool MinGobiAttrOperation(int di, std::string_view s) override {
            return Begin(20), Int(di), Str(s), Answer();
        }
        bool CheckGobiGAttr(std::string_view s, int di) override { return Begin(21), Str(s), Int(di), Answer(); }

    private:
        void Begin(int method) {
            s_->log += static_cast<char>(method);
            s_->log += static_cast<char>(static_cast<int8_t>(delta_));
            if (!s_->tracing) return;
            Text(kMethods[method]);
            Text("@");
            Signed(delta_);
            Text("(");
            args_ = 0;
        }
        void Sep() {
            if (s_->tracing && args_++) Text(",");
        }
        void Raw32(uint32_t v) {
            for (int i = 0; i < 4; i++) s_->log += static_cast<char>(v >> (8 * i) & 0xff);
        }
        void Int(int v) {
            Raw32(static_cast<uint32_t>(v));
            Sep();
            if (s_->tracing) Text(std::to_string(v));
        }
        void Str(std::string_view s) {
            Raw32(static_cast<uint32_t>(s.size()));
            s_->log += s;
            Sep();
            if (s_->tracing) Text("\""), Text(s), Text("\"");
        }
        void WordArg(Item* w) {
            const int d = w ? static_cast<Word*>(w)->delta_ : 0;
            s_->log += static_cast<char>(w ? static_cast<int8_t>(d) : 0x7f);
            Sep();
            if (!s_->tracing) return;
            if (w) Text("@"), Signed(d);
            else Text("nil");
        }
        void End() {
            if (s_->tracing) Text(")=");
        }
        int Result(int v) {
            End();
            args_ = 0;
            Int(v);
            if (s_->tracing) Text(" ");
            return v;
        }
        void Result(Item* w) {
            End();
            args_ = 0;
            WordArg(w);
            if (s_->tracing) Text(" ");
        }
        bool Answer() {
            if (s_->used == s_->script.size()) s_->script.push_back(false);
            const bool a = s_->script[s_->used++];
            End();
            s_->log += static_cast<char>(a);
            if (s_->tracing) Text(a ? "1 " : "0 ");
            return a;
        }
        void Text(std::string_view t) { s_->trace += t; }
        void Signed(int d) { Text(d < 0 ? "-" : "+"), Text(std::to_string(d < 0 ? -d : d)); }

        Scripted* s_ = nullptr;
        int delta_ = 0;
        int args_ = 0;
    };

    Word words_[255];
};

struct PathSummary {
    long long paths;
    uint64_t hash;
};

inline PathSummary Paths(const hdTrans::rules::Program& program, uint32_t root, Scripted& s) {
    uint64_t h = 14695981039346656037ull;
    s.script.clear();
    for (long long paths = 1;; paths++) {
        s.Restart();
        const bool result = hdTrans::rules::RunRule(program, root, s.Anchor());
        s.log += '\xff';
        s.log += static_cast<char>(result);
        for (char c : s.log) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
        s.script.resize(s.used);
        while (!s.script.empty() && s.script.back()) s.script.pop_back();
        if (s.script.empty()) return {paths, h};
        s.script.back() = true;
    }
}

}  // namespace rulestest
