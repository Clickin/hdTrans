// The analyze stage against $HDTRANS_GOLDEN/analyze: per case the input (IN on the raw
// path, the CH pieces of each chunk on the text path), the lookup environment of each
// chunk (ENV: plain entries, strategy, prior dictionary, confirmed and person keys,
// settings) and the expected LK/LKC records of the token loop and CM/L/LC/LD records
// of each sentence. The dictionaries are $HDTRANS_DICTS (default
// $HDTRANS_GOLDEN/dicts/base). Skipped when the set is absent.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/tokenizer/tokenize.h"
#include "hdTrans/word/analyze.h"
#include "hdTrans/word/worker.h"
#include "hdtest.h"
#include "word/analyze_records.h"

using namespace hdTrans;

namespace {

std::string GoldenDir() {
    if (const char* g = std::getenv("HDTRANS_GOLDEN"); g != nullptr && *g != 0) return g;
    std::string f = __FILE__;
    const size_t slash = f.find_last_of("/\\");
    return (slash == std::string::npos ? std::string(".") : f.substr(0, slash)) + "/../../../../hdtrans-golden";
}

std::string DictsDir() {
    if (const char* d = std::getenv("HDTRANS_DICTS"); d != nullptr && *d != 0) return d;
    return GoldenDir() + "/dicts/base";
}

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
            if (++shown <= 8) {
                hdtest::Fail(__FILE__, __LINE__, set + " " + chunk.header + " record " + std::to_string(i) + "\n  got  " +
                                                     std::string(g) + "\n  want " + std::string(w));
            }
        }
    };
    analyzetest::ForEachChunk(data, chunk, finish);
}

void RunSets(const char* name, bool (*select)(const std::string&)) {
    const std::string dir = FixtureDir();
    if (dir.empty() || Dictionaries() == nullptr) {
        std::printf("skip %s: no analyze fixtures (HDTRANS_GOLDEN/analyze/MANIFEST) or dictionaries\n", name);
        return;
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
    HD_CHECK_EQ(differing, 0LL);
}

}  // namespace

HD_TEST(analyze_golden_raw) {
    RunSets("analyze_golden_raw", [](const std::string& s) { return s == "corpus.raw"; });
}

HD_TEST(analyze_golden_each) {
    RunSets("analyze_golden_each", [](const std::string& s) { return s == "corpus2.each"; });
}

HD_TEST(analyze_golden_docs) {
    RunSets("analyze_golden_docs", [](const std::string& s) { return s.starts_with("doc-"); });
}
