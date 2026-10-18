#include "hdTrans/rules/rules.h"

#include <cstdio>
#include <string>
#include <string_view>

#include "hdtest.h"
#include "scripted_item.h"

using hdTrans::rules::LoadError;
using hdTrans::rules::Program;
using rulestest::Scripted;

namespace {

std::string SourceDir() {
    std::string f = __FILE__;
    const size_t slash = f.find_last_of("/\\");
    return slash == std::string::npos ? "." : f.substr(0, slash);
}

bool LoadSample(Program& p) {
    std::FILE* f = std::fopen((SourceDir() + "/testdata/sample.flat").c_str(), "rb");
    if (f == nullptr) return false;
    std::string text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    LoadError e{};
    return hdTrans::rules::Load(text, p, e);
}

void SetScript(Scripted& s, std::string_view answers) {
    s.script.clear();
    for (char c : answers) s.script.push_back(c == '1');
    s.Restart();
    s.tracing = true;
}

}  // namespace

// Number of evaluation paths of every sample rule, counted by hand.
HD_TEST(rules_sample_paths) {
    Program p;
    HD_REQUIRE(LoadSample(p));
    HD_REQUIRE(p.localRules.size() == 20 && p.globalRules.size() == 6);
    const long long local[] = {1, 2, 3, 3, 3, 2, 3, 4, 2, 4, 3, 4, 3, 3, 2, 7, 9, 5, 6, 5};
    const long long global[] = {3, 2, 2, 2, 2, 2};
    Scripted s;
    for (size_t i = 0; i < p.localRules.size(); i++)
        HD_CHECK_EQ(rulestest::Paths(p, p.localRules[i], s).paths, local[i]);
    for (size_t i = 0; i < p.globalRules.size(); i++)
        HD_CHECK_EQ(rulestest::Paths(p, p.globalRules[i].root, s).paths, global[i]);
    HD_CHECK_EQ(p.globalRules[4].pumsa, static_cast<unsigned char>('4'));
    HD_CHECK_EQ(p.globalRules[4].number, 5200);
    HD_CHECK(!p.globalRules[4].uganRule && p.globalRules[3].uganRule);
}

// The calls of one evaluation, in order, for given answers of the Check and
// Assign methods.
HD_TEST(rules_sample_calls) {
    Program p;
    HD_REQUIRE(LoadSample(p));
    struct Case {
        int rule;
        const char* answers;
        bool result;
        const char* trace;
    };
    const Case cases[] = {
        {0, "", false, ""},
        {1, "1", true, "CheckSrc@+0(\"あら\")=1 "},
        // != is true when the check is false; a word operand at +0 is the anchor.
        {2, "0", true, "GetObject@+0(-1)=@-1 CheckSrc@-1(\"Lの\")=0 "},
        {2, "11", true,
         "GetObject@+0(-1)=@-1 CheckSrc@-1(\"Lの\")=1 GetObject@+0(1)=@+1 CheckLLSrc@+0(@+1)=1 "},
        {3, "1", true,
         "GetObject@+0(1)=@+1 GetCurrentCompDI@+1()=11 CheckDst@+1(\"R가\",11)=1 "},
        {3, "01", false,
         "GetObject@+0(1)=@+1 GetCurrentCompDI@+1()=11 CheckDst@+1(\"R가\",11)=0 "
         "GetObject@+0(-1)=@-1 GetCurrentCompDI@-1()=9 CheckDst@-1(\"\",9)=1 "},
        {5, "1", true, "GetObject@+0(-1)=@-1 GetCurrentCompDI@-1()=9 AssignLLDst@-1(@+0,9)=1 "},
        // & evaluates its second operand after a false first one.
        {7, "11", false,
         "GetCurrentCompDI@+0()=10 CheckAttr@+0(\"QID\",10)=1 "
         "GetCurrentCompDI@+0()=10 MinAttrOperation@+0(10,\"VHn\")=1 "},
        {8, "1", true, "GetObject@+0(-2)=@-2 GetCurrentCompDI@+0()=10 CheckLLAttr@+0(6,@-2,10)=1 "},
        {9, "011", true,
         "GetCurrentCompDI@+0()=10 CheckGAttr@+0(\"Ow\",10)=0 "
         "GetObject@+0(1)=@+1 GetCurrentCompDI@+1()=11 CheckGAttr@+1(\"Xj\",11)=1 "
         "GetObject@+0(-1)=@-1 GetCurrentCompDI@-1()=9 MinGAttrOperation@-1(9,\"Hd\")=1 "},
        {10, "01", false,
         "CheckGobiSrc@+0(\"Lて\")=0 GetObject@+0(1)=@+1 CheckGobiSrc@+1(\"た\")=1 "},
        {11, "111", true,
         "GetCurrentCompUmiDI@+0()=20 CheckGobiDst@+0(\"1고\",20)=1 "
         "GetCurrentCompUmiDI@+0()=20 CheckGobiPumsa@+0(\"57*\",20)=1 "
         "GetCurrentCompUmiDI@+0()=20 AssignGobiDst@+0(20,\"6서\")=1 "},
        // Y = @d takes the base candidate index.
        {12, "10", false,
         "GetObject@+0(-1)=@-1 GetCurrentCompDI@-1()=9 AssignLLGobiDst@-1(@+0,9)=1 "
         "GetCurrentCompUmiDI@+0()=20 CheckGobiAttr@+0(\"VHn\",20)=0 "},
        {13, "01", true,
         "GetCurrentCompUmiDI@+0()=20 MinGobiAttrOperation@+0(20,\"VMo\")=0 "
         "GetCurrentCompUmiDI@+0()=20 CheckGobiGAttr@+0(\"Da\",20)=1 "},
        {14, "0", true, "GetObject@+0(-1)=@-1 GetCurrentCompUmiDI@-1()=19 CheckGobiGAttr@-1(\"Ds\",19)=0 "},
        // An assignment made before a later false operand is not undone; the
        // next statement still runs.
        {15, "1101", true,
         "GetObject@+0(1)=@+1 CheckSrc@+1(\"で\")=1 GetCurrentCompDI@+0()=10 AssignDst@+0(10,\"에서\")=1 "
         "GetObject@+0(-1)=@-1 GetCurrentCompUmiDI@-1()=19 AssignGobiDst@-1(19,\"\")=0 "
         "CheckSrc@+0(\"\x82\xa0\")=1 "},
        {16, "1", false,
         "GetObject@+0(1)=@+1 CheckSrc@+1(\"a\")=1 GetObject@+0(1)=@+1 CheckSrc@+1(\"b\")=0 "
         "GetObject@+0(1)=@+1 CheckSrc@+1(\"c\x82\")=0 "},
        {16, "0000", false,
         "GetObject@+0(1)=@+1 CheckSrc@+1(\"a\")=0 GetObject@+0(1)=@+1 CheckSrc@+1(\"b\")=0 "
         "GetObject@+0(1)=@+1 CheckSrc@+1(\"c\x82\")=0 GetObject@+0(1)=@+1 GetCurrentCompDI@+1()=11 "
         "CheckPumsa@+1(\"7**\",11)=0 "},
        {17, "011", true,
         "GetObject@+0(3)=@+3 CheckSrc@+3(\"\"も\\\")=0 GetObject@+0(-3)=@-3 CheckSrc@-3(\"x y\")=1 "
         "GetCurrentCompDI@+0()=10 CheckPumsa@+0(\"5:0\",10)=1 "},
        {18, "1011", true,
         "CheckSrc@+0(\"が\")=1 GetObject@+0(-1)=@-1 GetCurrentCompDI@-1()=9 CheckPumsa@-1(\"1**\",9)=0 "
         "GetObject@+0(-1)=@-1 GetCurrentCompDI@-1()=9 CheckPumsa@-1(\"2**\",9)=1 "
         "GetCurrentCompDI@+0()=10 AssignDst@+0(10,\" 이\")=1 "},
        {19, "101", true,
         "GetCurrentCompDI@+0()=10 AssignDst@+0(10,\"가\")=1 "
         "GetObject@+0(-1)=@-1 GetCurrentCompDI@-1()=9 AssignDst@-1(9,\"나\")=0 CheckSrc@+0(\"다\")=1 "},
    };
    Scripted s;
    for (const Case& c : cases) {
        SetScript(s, c.answers);
        const bool got = hdTrans::rules::RunLocal(p, s.Anchor(), c.rule);
        if (got != c.result || s.trace != c.trace)
            hdtest::Fail(__FILE__, __LINE__,
                         "rule " + std::to_string(c.rule) + " answers " + c.answers + ": " +
                             hdtest::Show(got) + " " + hdtest::Show(s.trace));
    }
}

// Missing neighbours: every operator on them is false without a call, != included;
// a missing right word is passed as nullptr, except to A -= @d, which is false.
HD_TEST(rules_missing_words) {
    Program p;
    HD_REQUIRE(LoadSample(p));
    Scripted s;
    s.lo = s.hi = 0;
    struct Case {
        int rule;
        bool result;
        const char* trace;
    };
    const Case cases[] = {
        {2, false, "GetObject@+0(-1)=nil GetObject@+0(1)=nil CheckLLSrc@+0(nil)=0 "},
        {3, false, "GetObject@+0(1)=nil GetObject@+0(-1)=nil "},
        {8, false, "GetObject@+0(-2)=nil "},
        {14, false, "GetObject@+0(-1)=nil "},
    };
    for (const Case& c : cases) {
        SetScript(s, "");
        const bool got = hdTrans::rules::RunLocal(p, s.Anchor(), c.rule);
        HD_CHECK_EQ(got, c.result);
        HD_CHECK_EQ(s.trace, std::string(c.trace));
    }
    SetScript(s, "1");
    HD_CHECK(!hdTrans::rules::RunLocal(p, nullptr, 1));
    HD_CHECK(!hdTrans::rules::RunLocal(p, s.Anchor(), -1));
    HD_CHECK(!hdTrans::rules::RunLocal(p, s.Anchor(), 20));
    HD_CHECK_EQ(s.trace, std::string());
}

// Global dispatch: pumsa key and stem flag select the rows, stem rows also by
// number, and the first firing row wins.
HD_TEST(rules_global_dispatch) {
    Program p;
    HD_REQUIRE(LoadSample(p));
    struct Case {
        bool ugan;
        char pumsa;
        bool last;
        const char* answers;
        bool result;
        const char* trace;
    };
    const Case cases[] = {
        {true, '1', false, "11", true,
         "GetCurrentCompDI@+0()=10 CheckPumsa@+0(\"1**\",10)=1 GetCurrentCompDI@+0()=10 AssignDst@+0(10,\"일\")=1 "},
        // 4999 runs, 5000 does not.
        {true, '1', false, "0", false, "GetCurrentCompDI@+0()=10 CheckPumsa@+0(\"1**\",10)=0 CheckSrc@+0(\"の\")=0 "},
        {true, '1', true, "1", false, ""},
        {true, '4', true, "1", true, "CheckSrc@+0(\"い\")=1 "},
        {true, '4', false, "1", false, ""},
        // Ending rows are not restricted by number.
        {false, '4', false, "01", true,
         "GetCurrentCompUmiDI@+0()=20 CheckGobiDst@+0(\"6\",20)=0 GetCurrentCompUmiDI@+0()=20 CheckGobiDst@+0(\"6서\",20)=1 "},
        {false, '4', true, "1", true, "GetCurrentCompUmiDI@+0()=20 CheckGobiDst@+0(\"6\",20)=1 "},
        {false, '1', false, "1", false, ""},
    };
    Scripted s;
    for (const Case& c : cases) {
        SetScript(s, c.answers);
        const bool got = hdTrans::rules::RunGlobal(p, s.Anchor(), c.ugan, static_cast<unsigned char>(c.pumsa), c.last);
        HD_CHECK_EQ(got, c.result);
        HD_CHECK_EQ(s.trace, std::string(c.trace));
    }
}

HD_TEST(rules_load_errors) {
    struct Case {
        const char* text;
        int line;
        const char* message;  // nullptr: loads
    };
    const std::string deep = "rules 1 1 0\nL 0 " + [] {
        std::string s;
        for (int i = 0; i < 40; i++) s += "||1 ";
        return s + "S+0== \"x\"";
    }();
    const Case cases[] = {
        {"# comment\n\nrules 1 1 0\r\nL 0 ||1 S+0== \"x\"\n", 0, nullptr},
        {"", 0, "no header"},
        {"rules 2 0 0", 1, "bad number"},
        {"rules 1 0 0 0", 1, "bad header"},
        {"rules 1 1 0\nL 1 ||0", 2, "bad number"},
        {"rules 1 2 0\nL 1 ||0\nL 0 ||0", 2, "rule index out of order"},
        {"rules 1 2 0\nL 0 ||0", 2, "rule count differs from the header"},
        {"rules 1 1 1\nG 0 1 1002 1 ||0\nL 0 ||0", 3, "local rule after a global rule"},
        {"rules 1 1 0\nX 0 ||0", 2, "unknown record"},
        {"rules 1 1 0\nL 0 S+0== \"x\"", 2, "rule body must be a || list"},
        {"rules 1 1 0\nL 0 ||1 Q+0== \"x\"", 2, "unknown operator"},
        {"rules 1 1 0\nL 0 ||1 P+0= \"x\"", 2, "unknown operator"},
        {"rules 1 1 0\nL 0 ||1 P+0== @+1", 2, "unknown operator"},
        {"rules 1 1 0\nL 0 ||1 S0== \"x\"", 2, "bad atom delta"},
        {"rules 1 1 0\nL 0 ||1 S+0== @1", 2, "bad word operand"},
        {"rules 1 1 0\nL 0 ||1 S+0== x", 2, "bad operand"},
        {"rules 1 1 0\nL 0 ||1 S+0== \"x", 2, "unterminated string literal"},
        {"rules 1 1 0\nL 0 ||1 S+0== \"\\n\"", 2, "bad escape in string literal"},
        {"rules 1 1 0\nL 0 ||1 S+0== \"\\x4\"", 2, "bad escape in string literal"},
        {"rules 1 1 0\nL 0 ||2 S+0== \"x\"", 2, "record ends early"},
        {"rules 1 1 0\nL 0 ||1 S+0== \"x\" S+0== \"y\"", 2, "trailing tokens"},
        {"rules 1 1 0\nL 0 ||1  S+0== \"x\"", 2, "empty token"},
        {"rules 1 1 0\nL 0 ||1 S+0== \"x\" ", 2, "tokens must be separated by one space"},
        {"rules 1 1 0\nL 0 ||x", 2, "bad connective"},
        {"rules 1 0 1\nG 0 12 1002 1 ||0", 2, "pumsa key must be one byte"},
        {"rules 1 0 1\nG 0 1 1002 2 ||0", 2, "bad number"},
        {deep.c_str(), 2, "expression nested too deeply"},
    };
    for (const Case& c : cases) {
        Program p;
        LoadError e{};
        const bool ok = hdTrans::rules::Load(c.text, p, e);
        if (c.message == nullptr) {
            HD_CHECK(ok);
            continue;
        }
        if (ok || e.line != c.line || std::string_view(e.message) != c.message)
            hdtest::Fail(__FILE__, __LINE__,
                         std::string(c.text) + ": " + (ok ? "loaded" : std::to_string(e.line) + " " + e.message));
    }
}

// Literal bytes survive the escapes as they are, invalid UTF-8 included.
HD_TEST(rules_load_literals) {
    Program p;
    LoadError e{};
    HD_REQUIRE(hdTrans::rules::Load("rules 1 1 0\nL 0 ||3 S+0== \"\\\\\\\"\" P+0== \"\\xcc\\xBF\xfb\" Y-1= \"\"",
                                    p, e));
    HD_REQUIRE(p.nodes.size() == 4);
    auto lit = [&](size_t i) { return p.literals.substr(p.nodes[i].first, p.nodes[i].second); };
    HD_CHECK_EQ(lit(1), std::string("\\\""));
    HD_CHECK_EQ(lit(2), std::string("\xcc\xbf\xfb"));
    HD_CHECK_EQ(lit(3), std::string());
    HD_CHECK_EQ(p.nodes[3].delta, -1);
}
