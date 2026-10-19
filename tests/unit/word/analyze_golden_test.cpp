// The analyze stage against $HDTRANS_GOLDEN/analyze: per case the input (IN on the raw
// path, the CH pieces of each chunk on the text path), the lookup environment of each
// chunk (ENV: plain entries, strategy, prior dictionary, confirmed and person keys,
// settings) and the expected LK/LKC records of the token loop and CM/L/LC/LD records
// of each sentence. The dictionaries are $HDTRANS_DICTS. Skipped when the set is absent.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/tokenizer/tokenize.h"
#include "core/word/analyze.h"
#include "core/word/worker.h"
#include "hdtest.h"
#include "golden.h"
#include "word/analyze_records.h"

using namespace hdTrans;

namespace {

using hdtest::GoldenDir;

using hdtest::DictsDir;

// The analyze fixtures, empty when absent or incomplete (complete once MANIFEST exists).
std::string FixtureDir() {
    const std::string dir = GoldenDir() + "/analyze/";
    std::FILE* m = std::fopen((dir + "MANIFEST").c_str(), "rb");
    if (m == nullptr) return {};
    std::fclose(m);
    return dir;
}

const dict::Snapshot* Dictionaries() {
    static std::shared_ptr<const dict::Snapshot> snapshot = [] {
        dict::LoadResult r = dict::LoadFolders({DictsDir()});
        return r.snapshot;
    }();
    return snapshot.get();
}

struct Totals {
    std::map<std::string, long long> records, differing;
    long long chunks = 0;
    double seconds = 0;
};

void RunSet(const std::string& dir, const std::string& set, analyzetest::Environments& envs, Totals& totals) {
    std::string data;
    if (!analyzetest::ReadFile(dir + set + ".tsv", data)) {
        hdtest::Fail(__FILE__, __LINE__, "cannot read " + set);
        return;
    }
    const auto before = totals.records;
    const auto expectedCounts = hdtest::RecordCounts(data);
    const auto chunksBefore = totals.chunks;
    const dict::Snapshot* snapshot = Dictionaries();
    tokenizer::Tokens tokens;
    word::Arena arena;
    word::Analysis analysis;
    analyzetest::Chunk chunk;
    std::vector<std::string> got;
    int shown = 0;
    auto finish = [&] {
        if (!chunk.active) return;
        chunk.active = false;
        const analyzetest::Environment* env = envs.Get(chunk.env);
        if (env == nullptr) {
            hdtest::Fail(__FILE__, __LINE__, set + ": no environment " + std::to_string(chunk.env));
            return;
        }
        if (env->settings.text) tokenizer::TokenizeSpans(chunk.pieces, true, tokens);
        else tokenizer::TokenizeSpans(chunk.pieces, false, tokens);
        got.clear();
        const auto start = std::chrono::steady_clock::now();
        analyzetest::Analyze(tokens, *snapshot, *env, chunk.index, chunk.faultSentence, arena, analysis, got);
        totals.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        totals.chunks++;
        const size_t n = std::max(got.size(), chunk.want.size());
        for (size_t i = 0; i < n; i++) {
            const std::string_view g = i < got.size() ? std::string_view(got[i]) : "<none>";
            const std::string_view w = i < chunk.want.size() ? std::string_view(chunk.want[i]) : "<none>";
            const std::string tag(analyzetest::Tag(i < chunk.want.size() ? w : g));
            totals.records[tag]++;
            if (g == w) continue;
            totals.differing[tag]++;
            if (++shown <= 10) {
                hdtest::Fail(__FILE__, __LINE__, set + " " + chunk.header + " record " + std::to_string(i) + "\n  got  " +
                                                     std::string(g) + "\n  want " + std::string(w));
            }
        }
    };
    analyzetest::ForEachChunk(data, chunk, finish);
    HD_CHECK_EQ(totals.chunks - chunksBefore, static_cast<long long>(hdtest::CountRecords(data, "ENV")));
    for (const char* tag : {"LK", "LKC", "CM", "L", "LC", "LD", "LX"}) {
        const auto old = before.find(tag);
        const auto want = expectedCounts.find(tag);
        HD_CHECK_EQ(totals.records[tag] - (old == before.end() ? 0 : old->second),
                    static_cast<long long>(want == expectedCounts.end() ? 0 : want->second));
    }
}

void RunSets(const char* name, bool (*select)(const std::string&)) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const std::string dir = FixtureDir();
    if (dir.empty() || Dictionaries() == nullptr) {
        HD_SKIP("no analyze fixtures (HDTRANS_GOLDEN/analyze/MANIFEST) or dictionaries");
    }
    std::vector<std::string> sets;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string f = it->path().filename().string();
        if (f.size() > 4 && f.ends_with(".tsv") && f != "env.tsv" && f != "katakana.tsv") {
            const std::string set = f.substr(0, f.size() - 4);
            if (select(set)) sets.push_back(set);
        }
    }
    std::sort(sets.begin(), sets.end());
    HD_REQUIRE(!ec);
    HD_REQUIRE(!sets.empty());
    analyzetest::Environments envs(dir);
    Totals totals;
    for (const std::string& set : sets) RunSet(dir, set, envs, totals);
    long long differing = 0;
    std::printf("%s: %zu sets, %lld chunks, analyze %.2f s\n", name, sets.size(), totals.chunks, totals.seconds);
    for (const auto& [tag, n] : totals.records) {
        const long long d = totals.differing[tag];
        differing += d;
        std::printf("  %-4s %10lld records, %lld differing\n", tag.c_str(), n, d);
    }
    HD_CHECK(totals.chunks > 0);
    HD_CHECK_EQ(differing, 0LL);
}

}  // namespace

HD_TEST(analyze_golden_raw) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/analyze/"));
    RunSets("analyze_golden_raw", [](const std::string& s) { return s == "corpus.raw"; });
}

HD_TEST(analyze_golden_each) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/analyze/"));
    RunSets("analyze_golden_each", [](const std::string& s) { return s == "corpus2.each"; });
}

HD_TEST(analyze_golden_docs) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/analyze/"));
    RunSets("analyze_golden_docs", [](const std::string& s) { return s.starts_with("doc-"); });
}
