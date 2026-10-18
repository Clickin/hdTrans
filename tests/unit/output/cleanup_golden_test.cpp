// The cleanup against the stage records of the expectation sets: for every case or
// document line of $HDTRANS_GOLDEN/stages/<set>.K.tsv, the sentence pieces (K) run
// through Cleanup must give the C records of <set>.C.tsv. The raw sets end at K and
// are left out. Skipped when the stages folder is absent.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "hdTrans/output/cleanup.h"
#include "hdtest.h"
#include "stage_records.h"

using namespace hdTrans::output;
using namespace stagerecords;

namespace {

int Int(std::string_view s) { return std::atoi(std::string(s).c_str()); }

std::vector<std::string> CleanupRecords(const std::vector<std::string_view>& kRecords) {
    std::vector<Sentence> sentences;
    int chunk = -1, sent = -1;
    for (const auto r : kRecords) {
        const auto f = Split(r, '\t');
        if (f[0] != "K") continue;
        const int c = Int(f[1]), s = Int(f[2]);
        if (sentences.empty() || c != chunk || s != sent) {
            sentences.push_back({c, {}});
            chunk = c;
            sent = s;
        }
        sentences.back().pieces.push_back({UnescapeStageText(f[5]), static_cast<SpanKind>(Int(f[4]))});
    }
    std::vector<std::string> records;
    const Pieces out = Cleanup(sentences);
    for (size_t i = 0; i < out.size(); i++) {
        records.push_back("C\t" + std::to_string(i) + "\t" + std::to_string(static_cast<int>(out[i].span)) + "\t" +
                          EscapeStageText(out[i].text));
    }
    return records;
}

}  // namespace

HD_TEST(output_cleanup_golden) {
    const std::string dir = GoldenDir() + "/stages/";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        std::printf("skip output_cleanup_golden: no %s\n", dir.c_str());
        return;
    }
    // The set is complete when its MANIFEST is written.
    HD_REQUIRE(std::filesystem::exists(dir + "MANIFEST", ec));
    std::vector<std::string> sets;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        const std::string suffix = ".K.tsv";
        if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        const std::string set = name.substr(0, name.size() - suffix.size());
        if (set.size() >= 4 && set.compare(set.size() - 4, 4, ".raw") == 0) continue;
        sets.push_back(set);
    }
    std::sort(sets.begin(), sets.end());
    HD_REQUIRE(!sets.empty());

    size_t cases = 0, records = 0, differing = 0;
    for (const auto& set : sets) {
        std::string kFile, cFile;
        HD_REQUIRE(ReadFile(dir + set + ".K.tsv", kFile));
        HD_REQUIRE(ReadFile(dir + set + ".C.tsv", cFile));
        const auto kBlocks = Blocks(kFile);
        const auto cBlocks = Blocks(cFile);
        HD_REQUIRE(kBlocks.size() == cBlocks.size());
        for (size_t b = 0; b < kBlocks.size(); b++) {
            HD_REQUIRE(kBlocks[b].header == cBlocks[b].header);
            const auto got = CleanupRecords(kBlocks[b].records);
            const auto& want = cBlocks[b].records;
            cases++;
            records += want.size();
            const size_t n = std::max(got.size(), want.size());
            for (size_t i = 0; i < n; i++) {
                const std::string_view g = i < got.size() ? std::string_view(got[i]) : "";
                const std::string_view w = i < want.size() ? want[i] : "";
                if (g == w) continue;
                if (differing++ < 10) {
                    std::fprintf(stderr, "%s %.*s\n  want %.*s\n  got  %.*s\n", set.c_str(),
                                 static_cast<int>(kBlocks[b].header.size()), kBlocks[b].header.data(),
                                 static_cast<int>(w.size()), w.data(), static_cast<int>(g.size()), g.data());
                }
            }
        }
    }
    std::printf("output_cleanup_golden: %zu sets, %zu cases, %zu C records, %zu differing\n", sets.size(), cases,
                records, differing);
    HD_CHECK_EQ(differing, static_cast<size_t>(0));
}
