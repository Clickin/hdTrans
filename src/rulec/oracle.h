#pragma once
// A scripted Item for translation validation: two evaluations of a rule are equivalent
// when, for every script of answers, they make the same calls with the same arguments and
// return the same result.
//
// Every GetObject and every Check or Assign call takes the next answer of the script
// (false once it is used up): for GetObject, whether the word exists. A word at delta d
// returns 10+d from GetCurrentCompDI and 20+d from GetCurrentCompUmiDI. Paths enumerates
// the scripts of all evaluation paths depth first and hashes the call logs (FNV-1a 64).
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/rules/rules.h"

namespace hdTrans::rulec {

class Oracle {
public:
    std::vector<bool> script;
    std::size_t used = 0;
    std::string log;

    Oracle() {
        for (int i = 0; i < 255; i++) words_[i].Bind(this, i - 127);
    }
    Oracle(const Oracle&) = delete;
    Oracle& operator=(const Oracle&) = delete;

    rules::Item* Anchor() { return &words_[127]; }

    struct Summary {
        long long paths = 0;
        uint64_t hash = 14695981039346656037ull;
        bool capped = false;
    };

    // eval(anchor) runs the rule once.
    template <typename Eval>
    Summary Paths(Eval eval, long long cap) {
        Summary sum;
        script.clear();
        for (;;) {
            used = 0;
            log.clear();
            const bool result = eval(Anchor());
            log += '\xff';
            log += static_cast<char>(result);
            for (const char c : log) sum.hash = (sum.hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
            sum.paths++;
            script.resize(used);
            while (!script.empty() && script.back()) script.pop_back();
            if (script.empty()) return sum;
            if (sum.paths >= cap) {
                sum.capped = true;
                return sum;
            }
            script.back() = true;
        }
    }

private:
    class Word final : public rules::Item {
    public:
        void Bind(Oracle* o, int delta) { o_ = o, delta_ = delta; }

        Item* GetObject(int d) override {
            const int t = delta_ + d;
            Begin(0), Int(d);
            const bool exists = Answer() && t >= -127 && t <= 127;
            return exists ? &o_->words_[t + 127] : nullptr;
        }
        int GetCurrentCompDI() override { return Begin(1), 10 + delta_; }
        int GetCurrentCompUmiDI() override { return Begin(2), 20 + delta_; }

        bool CheckSrc(std::string_view s) override { return Begin(3), Str(s), Answer(); }
        bool CheckLLSrc(Item* wi) override { return Begin(4), WordArg(wi), Answer(); }
        bool CheckDst(std::string_view s, int di) override { return Begin(5), Str(s), Int(di), Answer(); }
        bool AssignDst(int di, std::string_view s) override { return Begin(6), Int(di), Str(s), Answer(); }
        bool AssignLLDst(Item* wi, int di) override { return Begin(7), WordArg(wi), Int(di), Answer(); }
        bool CheckPumsa(std::string_view s, int di) override { return Begin(8), Str(s), Int(di), Answer(); }
        bool CheckAttr(std::string_view s, int di) override { return Begin(9), Str(s), Int(di), Answer(); }
        bool MinAttrOperation(int di, std::string_view s) override { return Begin(10), Int(di), Str(s), Answer(); }
        bool CheckLLAttr(int op, Item* wi, int di) override { return Begin(11), Int(op), WordArg(wi), Int(di), Answer(); }
        bool CheckGAttr(std::string_view s, int di) override { return Begin(12), Str(s), Int(di), Answer(); }
        bool MinGAttrOperation(int di, std::string_view s) override { return Begin(13), Int(di), Str(s), Answer(); }
        bool CheckGobiSrc(std::string_view s) override { return Begin(14), Str(s), Answer(); }
        bool CheckGobiDst(std::string_view s, int di) override { return Begin(15), Str(s), Int(di), Answer(); }
        bool AssignGobiDst(int di, std::string_view s) override { return Begin(16), Int(di), Str(s), Answer(); }
        bool AssignLLGobiDst(Item* wi, int di) override { return Begin(17), WordArg(wi), Int(di), Answer(); }
        bool CheckGobiPumsa(std::string_view s, int di) override { return Begin(18), Str(s), Int(di), Answer(); }
        bool CheckGobiAttr(std::string_view s, int di) override { return Begin(19), Str(s), Int(di), Answer(); }
        bool MinGobiAttrOperation(int di, std::string_view s) override { return Begin(20), Int(di), Str(s), Answer(); }
        bool CheckGobiGAttr(std::string_view s, int di) override { return Begin(21), Str(s), Int(di), Answer(); }

    private:
        void Begin(int method) {
            o_->log += static_cast<char>(method);
            o_->log += static_cast<char>(static_cast<int8_t>(delta_));
        }
        void Int(int v) {
            for (int i = 0; i < 4; i++) o_->log += static_cast<char>(static_cast<uint32_t>(v) >> (8 * i) & 0xff);
        }
        void Str(std::string_view s) {
            Int(static_cast<int>(s.size()));
            o_->log += s;
        }
        void WordArg(Item* w) { o_->log += static_cast<char>(w ? static_cast<Word*>(w)->delta_ : 0x7f); }
        bool Answer() {
            if (o_->used == o_->script.size()) o_->script.push_back(false);
            const bool a = o_->script[o_->used++];
            o_->log += static_cast<char>(a);
            return a;
        }

        Oracle* o_ = nullptr;
        int delta_ = 0;
    };

    Word words_[255];
};

}  // namespace hdTrans::rulec
