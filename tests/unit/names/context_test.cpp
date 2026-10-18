// Table tests of the per-line facts that the golden sets do not exercise: the names cued
// by speaker labels of a text without a document, and the whole-line rows of lines.tsv.
// Needs the base dictionary ($HDTRANS_DICTS); skipped when it is absent.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/names/document.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

std::string DictsDir() {
    if (const char* d = std::getenv("HDTRANS_DICTS"); d != nullptr && *d != 0) return d;
    std::string f = __FILE__;
    const size_t slash = f.find_last_of("/\\");
    return (slash == std::string::npos ? std::string(".") : f.substr(0, slash)) + "/../../../../dicts/base";
}

const dict::Snapshot* Base() {
    static const dict::LoadResult loaded = dict::LoadFolders({DictsDir()});
    return loaded.snapshot.get();
}

}  // namespace

HD_TEST(names_cued_names) {
    const dict::Snapshot* snap = Base();
    if (snap == nullptr) return;
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
    const dict::Snapshot* snap = Base();
    if (snap == nullptr) return;
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
