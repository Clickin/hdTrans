#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "hdTrans/engine.h"
#include "hdTrans/engine/driver.h"
#include "hdTrans/word/select.h"
#include "hdtest.h"
#include "golden.h"
#include "tokenizer/stage_records.h"

using namespace hdTrans;

namespace {

std::vector<std::string> Lines(const std::string& text, bool keepTrailing) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            if (keepTrailing || start < text.size()) lines.push_back(text.substr(start));
            return lines;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

struct SelectStats {
    std::map<word::Provenance, std::size_t> layers;
    std::size_t items = 0, scored = 0, wouldChange = 0, blocked = 0;
};

void CountChoices(void* context, int, int, const word::Chain& chain) {
    auto& stats = *static_cast<SelectStats*>(context);
    for (const word::Item* it = chain.head; it != nullptr; it = it->next) {
        stats.items++;
        stats.layers[it->choice.layer]++;
        if (!it->shadowChoices.empty()) stats.scored++;
        for (const word::ShadowChoice& choice : it->shadowChoices) {
            if (choice.winner) stats.wouldChange++;
            if (!choice.eligible && choice.candidate != choice.from) stats.blocked++;
        }
    }
}

std::string TranslateSet(const dict::Snapshot& snapshot, const std::string& input, const std::string& set,
                         const internal::TranslationOptions& options, const internal::StageObserver& observer) {
    const auto generated = internal::NewGenerated(snapshot);
    const bool raw = set.ends_with(".raw"), each = raw || set.ends_with(".each");
    const std::vector<std::string> lines = Lines(input, !each);
    std::string out;
    if (each) {
        for (const std::string& line : lines) {
            if (raw) {
                out += internal::TranslateRawLine(snapshot, *generated, line, observer, options);
            } else {
                internal::Memory memory;
                const std::vector<std::string> one{line};
                out += internal::TranslateDocument(snapshot, *generated, one, memory, observer, options)[0];
            }
            out += '\n';
        }
    } else {
        internal::Memory memory;
        const auto translated = internal::TranslateDocument(snapshot, *generated, lines, memory, observer, options);
        for (std::size_t i = 0; i < translated.size(); i++) {
            if (i != 0) out += '\n';
            out += translated[i];
        }
    }
    return out;
}

using hdtest::DictsDir;

}  // namespace

HD_TEST(select_shadow_golden) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/stages/"));
    const char* root = std::getenv("HDTRANS_GOLDEN");
    if (root == nullptr || *root == 0) {
        HD_SKIP("no HDTRANS_GOLDEN");
    }
    const dict::LoadResult loaded = dict::LoadFolders({DictsDir()});
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->HasDictionary());
    std::error_code error;
    std::vector<std::filesystem::path> inputs;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (entry.path().extension() == ".in") inputs.push_back(entry.path());
    }
    HD_REQUIRE(!error);
    std::sort(inputs.begin(), inputs.end());
    HD_REQUIRE(!inputs.empty());
    HD_CHECK_EQ(inputs.size(), hdtest::ManifestSets(std::string(root) + "/stages/", ".S.tsv").size());
    const word::Scorer scorers[] = {{"shortest-target", word::ShortestTargetScore}};
    internal::TranslationOptions options;
    options.reference = true;
    options.scorers = scorers;
    options.selectShadow = true;
    SelectStats stats;
    bool counted = false;
    for (const auto& path : inputs) {
        const std::string set = path.stem().string();
        std::string input, expected;
        HD_REQUIRE(stagetest::ReadFile(path.string(), input));
        HD_REQUIRE(stagetest::ReadFile((path.parent_path() / (set + ".expected")).string(), expected));
        internal::StageObserver observer;
        if (set == "corpus2.each") {
            observer.context = &stats;
            observer.resolved = CountChoices;
            counted = true;
        }
        const std::string actual = TranslateSet(*loaded.snapshot, input, set, options, observer);
        HD_REQUIRE(!expected.empty());
        HD_CHECK_EQ(hdtest::LineCount(actual), hdtest::LineCount(expected));
        const bool identical = actual == expected;
        std::printf("select_shadow_golden %s: %s\n", set.c_str(), identical ? "identical" : "DIFFERENT");
        if (!identical) hdtest::Fail(__FILE__, __LINE__, "shadow output differs in " + set);
    }
    HD_REQUIRE(counted);
    if (counted) {
        HD_CHECK(stats.items > 0);
        HD_CHECK(stats.wouldChange > 0);
        std::printf("select_shadow corpus2.each: items=%zu scored=%zu would-change=%zu blocked=%zu\n",
                    stats.items, stats.scored, stats.wouldChange, stats.blocked);
        for (const auto& [layer, count] : stats.layers) {
            std::printf("select_provenance %s %zu\n", word::ProvenanceName(layer), count);
        }
    }
}

HD_TEST(engine_session_select_shadow) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/stages/"));
    const char* golden = std::getenv("HDTRANS_GOLDEN");
    if (golden == nullptr || *golden == 0) {
        HD_SKIP("no HDTRANS_GOLDEN");
    }
    Engine engine(true);
    HD_REQUIRE(engine.Open({DictsDir()}));
    const std::string input = "今日はいい天気ですね。\n彼は静かに頷いた。\n";
    Session plain(engine), shadow(engine);
    const std::string expected = plain.Translate(input);
    shadow.SetSelectShadow(true);
    HD_CHECK(shadow.Translate(input) == expected);
    shadow.Reset();
    HD_CHECK(shadow.Translate(input) == expected);
    shadow.SetSelectShadow(false);
    shadow.Reset();
    HD_CHECK(shadow.Translate(input) == expected);
}
