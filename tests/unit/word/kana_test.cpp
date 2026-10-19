#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "core/word/kana.h"
#include "hdtest.h"
#include "golden.h"

using namespace hdTrans::word;

namespace {

// TransliterateKatakana as text: "fault" when a context rule fails.
std::string Katakana(std::string_view in) {
    std::string out;
    return TransliterateKatakana(in, out) ? out : std::string("fault");
}

bool ReadFile(const std::string& path, std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[1 << 16];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    return true;
}

}  // namespace

HD_TEST(kana_katakana_cases) {
    HD_CHECK_EQ(Katakana("コンピューター"), std::string("콘퓨타"));
    HD_CHECK_EQ(Katakana("メント"), std::string("먼트"));
    HD_CHECK_EQ(Katakana("ベッド"), std::string("벳드"));
    HD_CHECK_EQ(Katakana("ー"), std::string(""));
    HD_CHECK_EQ(Katakana("aアb"), std::string("a아b"));
    HD_CHECK_EQ(Katakana("イージーなユー"), std::string("이지な유"));
    HD_CHECK_EQ(Katakana("ヷルキューレ"), std::string("바르큐레"));
    HD_CHECK_EQ(Katakana("ヸヹヺ"), std::string("비베보"));
}

HD_TEST(kana_hiragana_cases) {
    HD_CHECK_EQ(TransliterateHiragana("さくら"), std::string("사쿠라"));
    HD_CHECK_EQ(TransliterateHiragana("かんぱい"), std::string("칸파이"));
    HD_CHECK_EQ(TransliterateHiragana("１ａ"), std::string("1a"));
    HD_CHECK_EQ(TransliterateHiragana("ゔぁいおりん"), std::string("바이오린"));
    HD_CHECK_EQ(TransliterateHiragana("ゔゕゖ"), std::string("부카케"));
}

// Every text of testdata/katakana.tsv (one or two katakana, and a third of the runs of
// three or more in the corpus2 text) with its expected transliteration, or "fault".
// Read from $HDTRANS_GOLDEN/analyze; skipped when absent.
HD_TEST(kana_katakana_fixture) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/analyze/"));
    const char* golden = std::getenv("HDTRANS_GOLDEN");
    if (golden == nullptr || *golden == 0) {
        HD_SKIP("HDTRANS_GOLDEN not set");
    }
    std::string data;
    if (!ReadFile(std::string(golden) + "/analyze/katakana.tsv", data)) {
        HD_SKIP("no katakana.tsv in HDTRANS_GOLDEN/analyze");
    }
    int n = 0, bad = 0;
    std::size_t pos = 0;
    while (pos < data.size()) {
        std::size_t end = data.find('\n', pos);
        if (end == std::string::npos) end = data.size();
        const std::string_view line(data.data() + pos, end - pos);
        pos = end + 1;
        if (line.empty() || line[0] == '#') continue;
        const std::size_t tab = line.find('\t');
        const std::string in(line.substr(0, tab));
        const std::string want(tab == std::string_view::npos ? std::string_view() : line.substr(tab + 1));
        n++;
        if (Katakana(in) != want) bad++;
    }
    HD_CHECK_EQ(bad, 0);
    size_t expectedRecords = 0;
    for (const auto& [tag, count] : hdtest::RecordCounts(data)) expectedRecords += count;
    HD_CHECK_EQ(n, 9481);
    HD_CHECK_EQ(static_cast<size_t>(n), expectedRecords);
}
