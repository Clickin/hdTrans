#include "core/filter/filter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/filter/ehnd.h"
#include "core/filter/matcher.h"
#include "core/filter/pattern.h"
#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::filter;

namespace {

using Kind = Source::Kind;

std::shared_ptr<const Filters> Make(std::vector<Source> sources, std::vector<Error>* errors = nullptr) {
    std::vector<Error> e;
    auto f = Build(sources, errors ? *errors : e);
    return f;
}

// line through Pre, then (with post) through Post with the toggles Pre left.
std::string Run(const Filters& f, std::string line, const char* post = nullptr) {
    Filters::Toggles t = f.Start();
    f.Apply(Stage::Pre, line, t);
    if (!post) return line;
    std::string out = post;
    f.Apply(Stage::Post, out, t);
    return line + " | " + out;
}

std::string Shown(const std::vector<Error>& errors) {
    std::string s;
    for (const Error& e : errors) s += (s.empty() ? "" : "\n") + e.file + ":" + std::to_string(e.line) + ": " + e.message;
    return s;
}

}  // namespace

HD_TEST(FilterLiteralRows) {
    // A replacement resumes after the text it inserted. "x\ty" with its newline has three
    // fields (layer 0); the same row at the end of the file without one has two and is
    // dropped.
    auto f = Make({{Kind::PreFilter, "p.txt", "//comment\na\taa\t1\t0\nx\ty\nq\tQ", false}});
    HD_CHECK_EQ(Run(*f, "aXa x q"), "aaXaa y q");
    HD_CHECK_EQ(Run(*f, ""), "");
    // A "//" ends the line; in the REGEX field "0//" reads 0.
    f = Make({{Kind::PreFilter, "p.txt", "b\tc\t1\t0//note\nd\te//f\t1\t0\n", false}});
    HD_CHECK_EQ(Run(*f, "bd"), "cd");
    // "#" lines are comments of native files only.
    f = Make({{Kind::PreFilter, "p.tsv", "# about\n#\tX\t1\t0\n", true}});
    HD_CHECK_EQ(Run(*f, "#"), "#");
    f = Make({{Kind::PreFilter, "p.txt", "#\tX\t1\t0\n", false}});
    HD_CHECK_EQ(Run(*f, "#"), "X");
}

HD_TEST(FilterRegexRows) {
    auto f = Make({{Kind::PreFilter, "p.txt",
                    "(\\d+)円\t$1엔\t1\t1\n"
                    "[ぁ-ん]+\t<$0>\t2\t1\n"
                    "た(?=[。])\tだ\t3\t1\t_AUX:X\n",
                    false}});
    HD_CHECK_EQ(Run(*f, "100円と20円"), "100엔<と>20엔");
    HD_CHECK_EQ(Run(*f, "見た。"), "見<た>。");
    // _AUX: the regex runs only when the text holds the AUX text.
    f = Make({{Kind::PreFilter, "p.txt", "た(?=。)\tだ\t3\t1\t_AUX:X\n", false}});
    HD_CHECK_EQ(Run(*f, "見た。"), "見た。");
    HD_CHECK_EQ(Run(*f, "X見た。"), "X見だ。");
    // A literal row with _AUX runs only with it too.
    f = Make({{Kind::PreFilter, "p.txt", "た\tだ\t3\t0\t_AUX:X\n", false}});
    HD_CHECK_EQ(Run(*f, "見た"), "見た");
    HD_CHECK_EQ(Run(*f, "X見た"), "X見だ");
}

HD_TEST(FilterLayerOrder) {
    // Rows run by layer, then in load order across files.
    auto f = Make({{Kind::PreFilter, "a.txt", "x\ty\t20\t0\nq\tx\t10\t0\nm\tA\t5\t0\n", false},
                   {Kind::PreFilter, "b.txt", "m\tB\t5\t0\n", false}});
    HD_CHECK_EQ(Run(*f, "q m"), "y A");
    f = Make({{Kind::PreFilter, "b.txt", "m\tB\t5\t0\n", false}, {Kind::PreFilter, "a.txt", "m\tA\t5\t0\n", false}});
    HD_CHECK_EQ(Run(*f, "m"), "B");
    // Post rows run only after translation.
    f = Make({{Kind::PostFilter, "o.txt", "개\t멍멍이\t1\t0\n", false}});
    HD_CHECK_EQ(Run(*f, "개", "개"), "개 | 멍멍이");
}

HD_TEST(FilterToggles) {
    const std::string conn = "_TGL\t_CONN:T_\tx\tX\t5\t0\n";
    auto f = Make({{Kind::PreFilter, "s.txt", "_TGL\t_DECR:T_\t_OFF\n", false}, {Kind::PreFilter, "p.txt", conn, false}});
    HD_CHECK_EQ(Run(*f, "x"), "x");
    f = Make({{Kind::PreFilter, "s.txt", "_TGL\t_DECR:T_\t_ON\n", false}, {Kind::PreFilter, "p.txt", conn, false}});
    HD_CHECK_EQ(Run(*f, "x"), "X");
    // The first declaration holds.
    f = Make({{Kind::PreFilter, "s.txt", "_TGL\t_DECR:T_\t_ON\n_TGL\t_DECR:T_\t_OFF\n", false},
              {Kind::PreFilter, "p.txt", conn, false}});
    HD_CHECK_EQ(Run(*f, "x"), "X");
    // A firing _ON row (here a check row) switches T_ on for the later rows of the line,
    // Post included; _OFF switches it off. The next line starts from the declaration.
    f = Make({{Kind::PreFilter, "p.txt",
               "_TGL\t_DECR:T_\t_OFF\n_TGL\t_ON:T_\tgo\t_MATCH_CHECK\t1\t0\n" + conn +
                   "_TGL\t_OFF:T_\tstop\t_MATCH_CHECK\t6\t0\n",
               false},
              {Kind::PostFilter, "o.txt", "_TGL\t_CONN:T_\ty\tY\t1\t0\n", false}});
    HD_CHECK_EQ(Run(*f, "go x", "y"), "go X | Y");
    HD_CHECK_EQ(Run(*f, "x", "y"), "x | y");
    HD_CHECK_EQ(Run(*f, "go stop x", "y"), "go stop X | y");
    // A _CONN row of an undeclared toggle is off.
    f = Make({{Kind::PreFilter, "p.txt", conn, false}});
    HD_CHECK_EQ(Run(*f, "x"), "x");
}

HD_TEST(FilterSkipLayer) {
    // The rows of layer 5 of Pre run only when the text matches every condition of it.
    auto f = Make({{Kind::PreFilter, "p.txt", "a\tb\t5\t0\nc\td\t6\t0\n", false},
                   {Kind::PostFilter, "o.txt", "a\tb\t5\t0\n", false},
                   {Kind::SkipLayer, "s.txt", "//PRE\nPRE\t5\tne+d\t1\t//note\nPOST\t5\tz\n", false}});
    HD_CHECK_EQ(Run(*f, "a c", "a"), "a d | a");
    HD_CHECK_EQ(Run(*f, "need a c", "za"), "need b d | zb");
    f = Make({{Kind::PreFilter, "p.txt", "a\tb\t5\t0\n", false},
              {Kind::SkipLayer, "s.txt", "PRE\t5\tn\nPRE\t5\tm\n", false}});
    HD_CHECK_EQ(Run(*f, "n a"), "n a");
    HD_CHECK_EQ(Run(*f, "m n a"), "m n b");
}

HD_TEST(FilterGuards) {
    // A firing _SkipLayer row opens the next run of _skip_sub rows; the run closes it.
    auto f = Make({{Kind::PreFilter, "p.txt",
                    "_SkipLayer\tK\t_MATCH_CHECK\t5\t0\n_skip_sub\ta\tb\t5\t0\n_skip_sub\tc\td\t5\t0\n"
                    "e\tf\t5\t0\n_skip_sub\tg\th\t5\t0\n_SkipLayer\tK\tL\t6\t0\n_skip_sub\tj\tJ\t6\t0\n",
                    false}});
    HD_CHECK_EQ(Run(*f, "K a c e g j"), "L b d f g J");
    HD_CHECK_EQ(Run(*f, "a c e g j"), "a c f g j");
}

HD_TEST(FilterRejectsLookbehind) {
    std::vector<Error> errors;
    auto f = Make({{Kind::PreFilter, "p.txt", "x\ty\t1\t0\n(?<=a)b\tc\t1\t1\n", false}}, &errors);
    HD_CHECK_EQ(Shown(errors), "p.txt:2: regex rejected (lookbehind is not supported): (?<=a)b");
    HD_CHECK_EQ(Run(*f, "abx"), "aby");
    errors.clear();
    Make({{Kind::SkipLayer, "s.txt", "PRE\t1\t(?<!a)b\n", false}}, &errors);
    HD_CHECK_EQ(Shown(errors), "s.txt:1: regex rejected (lookbehind is not supported): (?<!a)b");
}

HD_TEST(FilterUserDict) {
    std::vector<Error> errors;
    auto f = Make({{Kind::UserDict, "UserDict_a.txt",
                    "//c\nポチ\t포치#N\t1\tVHn\t//dog\n一応\t일단\t0\n門前\t문 앞#M\n"
                    "あいうえおかきくけこさしすせそた\t길다\t1\nx\ty//no second field\n",
                    false}},
                  &errors);
    HD_CHECK_EQ(Shown(errors), "UserDict_a.txt:5: the key is longer than 30 bytes in CP932");
    const dict::Dict* user = f->User();
    HD_REQUIRE(user != nullptr);
    HD_CHECK_EQ(user->Len(), std::size_t{3});
    auto show = [&](const char* key) {
        std::string s;
        for (const dict::Record& r : user->Records(key)) {
            s += std::string(r.target) + " " + r.codes->partText + " " + std::string(r.codes->sem.Text()) + " " +
                 std::string(r.codes->dict.Text());
        }
        return s;
    };
    HD_CHECK_EQ(show("ポチ"), "포치#N I110 VHn GN");
    HD_CHECK_EQ(show("一応"), "일단 A9D0  GN");
    HD_CHECK_EQ(show("門前"), "문 앞#M I110  GN");
}

HD_TEST(EhndText) {
    const std::string utf16("\xFF\xFE" "a\0\t\0\x10\xAC\r\0\n\0", 12);
    HD_CHECK_EQ(EhndText(utf16), "a\t감\n");
    HD_CHECK_EQ(EhndText("\xEF\xBB\xBFx\r\ny\rz"), "x\ny\rz");
}

HD_TEST(EhndFolderOrder) {
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) /
                     ("hdtrans-ehnd-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    HD_REQUIRE(std::filesystem::create_directory(dir, ec));
    for (const char* name : {"PreFilter_b.txt", "prefilter_A.txt", "PreFilter_A.txt.bak", "PostFilter.txt",
                             "UserDict#name.txt", "UserDict#1.txt", "SkipLayer.TXT", "other.txt"}) {
        std::ofstream(dir / name) << "x\n";
    }
    std::vector<Source> sources;
    std::vector<Error> errors;
    HD_CHECK(ReadEhndFolder(dir.string(), sources, errors));
    std::string names;
    for (const Source& s : sources) names += s.name + " ";
    HD_CHECK_EQ(names, "prefilter_A.txt PreFilter_b.txt PostFilter.txt SkipLayer.TXT UserDict#1.txt UserDict#name.txt ");
    std::filesystem::remove_all(dir, ec);
    HD_CHECK(!ReadEhndFolder((dir / "missing").string(), sources, errors));
}

HD_TEST(FilterPatternCheck) {
    struct Case {
        const wchar_t* pattern;
        const char* want;  // the literal, or "!" and the error
    };
    const Case cases[] = {
        {L"ませんでした(?:[のよ])(?=[Ⓖ-Ⓩ])", "ませんでした"},
        {L"a+b", "ab"},
        {L"x?yz", "yz"},
        {L"ab|cd", ""},
        {L"\\(か\\)", "(か)"},
        {L"(a)\\1", ""},
        {L"[^Ⓖ-Ⓩで]{1,3}", ""},
        {L"(?<=a)b", "!lookbehind is not supported"},
        {L"a?+", "!stacked quantifiers"},
        {L"a{2,1}", "!a {} count out of order"},
        {L"a{x}", "!malformed {}"},
        {L"\\q", "!an escape that ECMAScript does not define"},
        {L"\\ー", "!an escape of a non-ASCII character"},
        {L"[b-a]", "!a range out of order"},
        {L"[\\d-z]", "!a range bounded by a class"},
        {L"(a)\\2", "!a back-reference to a group not closed before it"},
        {L"(a\\1)", "!a back-reference to a group not closed before it"},
        {L"(a)(?=\\1)", "!a back-reference in a lookahead"},
        {L"(a*)*", "!nested unbounded repetition"},
        {L"a]", "!unmatched ] or }"},
        {L"^*", "!a quantifier after an assertion"},
        {L"(a", "!unterminated group"},
        {L"a)", "!unbalanced )"},
        {L"[a", "!unterminated class"},
        {L"[[:alpha:]]", "![: [. or [= in a class"},
    };
    for (const Case& c : cases) {
        std::wstring literal;
        std::string error;
        const bool ok = CheckPattern(c.pattern, literal, error);
        HD_CHECK_EQ(ok ? Narrow(literal) : "!" + error, std::string(c.want));
    }
}

HD_TEST(FilterMatcher) {
    Matcher m;
    const int he = m.Add(L"he"), she = m.Add(L"she"), his = m.Add(L"his"), hers = m.Add(L"hers");
    HD_CHECK_EQ(m.Add(L"she"), she);
    m.Build();
    std::vector<uint32_t> seen(m.Size());
    m.Scan(L"ushers", seen, 1);
    HD_CHECK(seen[static_cast<std::size_t>(he)] == 1 && seen[static_cast<std::size_t>(she)] == 1);
    HD_CHECK(seen[static_cast<std::size_t>(hers)] == 1 && seen[static_cast<std::size_t>(his)] == 0);
    m.Scan(L"this", seen, 2);
    HD_CHECK(seen[static_cast<std::size_t>(his)] == 2 && seen[static_cast<std::size_t>(he)] == 1);
}
