// Table tests of the per-line facts that the golden sets do not exercise: the names cued
// by speaker labels of a text without a document, and the whole-line rows of lines.tsv.
// Needs the base dictionary ($HDTRANS_DICTS); skipped when it is absent.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/names/document.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

using hdtest::DictsDir;

const dict::Snapshot* Base() {
    static const dict::LoadResult loaded = dict::LoadFolders({DictsDir()});
    return loaded.snapshot.get();
}

}  // namespace

HD_TEST(names_cued_names) {
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const dict::Snapshot* snap = Base();
    if (snap == nullptr) HD_SKIP("base dictionaries unavailable (HDTRANS_DICTS)");
    const std::vector<std::pair<std::string, std::vector<std::string>>> cases = {
        {"魅音「おはよう」", {"魅音"}},
        {"詩音さん", {}},
        {"睦月です。", {}},
        {"時雨、行くよ！", {"時雨"}},
        {"そう言って「おはよう」", {}},
        {"園崎詩音「こんにちは」", {"園崎詩音"}},
        {"photo\n魅音「あ」", {"魅音"}},
    };
    for (const auto& [text, want] : cases) {
        HD_CHECK(pipeline::CuedNames(*snap, text) == want);
    }
}

HD_TEST(names_whole_line_rows) {
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const dict::Snapshot* snap = Base();
    if (snap == nullptr) HD_SKIP("base dictionaries unavailable (HDTRANS_DICTS)");
    const std::vector<std::string> lines = {"つづきから"};
    auto doc = pipeline::BuildDocument(*snap, lines);
    HD_REQUIRE(doc != nullptr);
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"つづきから", "이어하기"},
        {"  つづきから  ", "  이어하기  "},
        {"つづきから!", ""},
        {"こんにちは", ""},
    };
    for (const auto& [line, want] : cases) {
        const pipeline::LineFacts facts = pipeline::ReadLine(*doc, line);
        HD_CHECK(facts.wholeLine.has_value() == !want.empty());
        if (facts.wholeLine) HD_CHECK(*facts.wholeLine == want);
    }
}

HD_TEST(names_confirmed_cache) {
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const dict::Snapshot* snap = Base();
    if (snap == nullptr) HD_SKIP("base dictionaries unavailable (HDTRANS_DICTS)");
    const std::vector<std::string> lines = {"光さん、", "光さん、", "光さん、"};
    auto doc = pipeline::BuildDocument(*snap, lines);
    HD_REQUIRE(doc->accepted.contains("光"));
    const std::vector<std::vector<std::string>> skips = {{}, {"光"}, {"光", "未登録"}};
    std::vector<pipeline::LineLookup> saved;
    for (const auto& skip : skips) {
        pipeline::LineFacts facts = pipeline::ReadLine(*doc, lines[0]);
        facts.skip = skip;
        const pipeline::LineLookup first = pipeline::LookupOf(*doc, facts);
        const pipeline::LineLookup again = pipeline::LookupOf(*doc, facts);
        HD_REQUIRE(first.confirmed != nullptr);
        HD_CHECK(first.confirmed == again.confirmed);
        HD_CHECK(first.plain == again.plain);
        HD_CHECK(pipeline::ContextOf(*doc, first).lookup.confirmed == first.confirmed);
        if (skip.empty()) HD_CHECK(first.confirmed == &doc->accepted);
        for (const std::string& name : doc->accepted) {
            const bool omitted = std::find(skip.begin(), skip.end(), name) != skip.end();
            HD_CHECK_EQ(first.confirmed->contains(name), !omitted);
        }
        saved.push_back(first);
    }
    HD_CHECK(saved[0].confirmed->contains("光"));
    HD_CHECK(!saved[1].confirmed->contains("光"));
    HD_CHECK(!saved[2].confirmed->contains("光"));
    HD_CHECK(doc->accepted.contains("光"));
}
