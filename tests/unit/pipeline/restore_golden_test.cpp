// The restore stage against the stage records of the expectation sets: for every case or
// document line of the text-path sets in $HDTRANS_GOLDEN/stages, the cleaned-up pieces (C)
// and the pieces as typed (P) run through RestoreAll must give the R1, R2 and R3 records
// and the OUT line. A line written whole from the line table (CTX line.wholeline) has no
// C to R3 records; its OUT is the CTX value. A line of the document ending in CR is
// translated without it and the CR is written back after the translation (IN record).
// Skipped when the stages folder is absent.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "hdTrans/pipeline/restore.h"
#include "hdtest.h"
#include "output/stage_records.h"

using namespace hdTrans;
using namespace stagerecords;

namespace {

int Int(std::string_view s) { return std::atoi(std::string(s).c_str()); }

output::Pieces ReadPieces(const std::vector<std::string_view>& records, std::string_view tag) {
    output::Pieces p;
    for (const auto r : records) {
        const auto f = Split(r, '\t');
        if (f[0] != tag || f.size() != 4) continue;
        p.push_back({UnescapeStageText(f[3]), static_cast<output::SpanKind>(Int(f[2]))});
    }
    return p;
}

void WriteRecords(std::vector<std::string>& out, std::string_view tag, const output::Pieces& p) {
    for (size_t i = 0; i < p.size(); i++) {
        out.push_back(std::string(tag) + "\t" + std::to_string(i) + "\t" + std::to_string(static_cast<int>(p[i].span)) +
                      "\t" + EscapeStageText(p[i].text));
    }
}

// The text of the IN record of a line (the line as read, CR included).
std::string InText(const std::vector<std::string_view>& records) {
    for (const auto r : records) {
        if (r.substr(0, r.find('\t')) == "IN") return UnescapeStageText(r.substr(r.find('\t') + 1));
    }
    return {};
}

std::vector<std::string_view> Tagged(const std::vector<std::string_view>& records, std::string_view tag) {
    std::vector<std::string_view> out;
    for (const auto r : records) {
        if (r.substr(0, r.find('\t')) == tag) out.push_back(r);
    }
    return out;
}

struct Totals {
    size_t blocks = 0, wholeLines = 0, records[4] = {}, differing[4] = {};
};

constexpr std::string_view kTags[4] = {"R1", "R2", "R3", "OUT"};

void Compare(const std::string& set, std::string_view header, int k, const std::vector<std::string>& got,
             const std::vector<std::string_view>& want, Totals& t) {
    t.records[k] += want.size();
    const size_t n = std::max(got.size(), want.size());
    for (size_t i = 0; i < n; i++) {
        const std::string_view g = i < got.size() ? std::string_view(got[i]) : "";
        const std::string_view w = i < want.size() ? want[i] : "";
        if (g == w) continue;
        size_t all = 0;
        for (const size_t d : t.differing) all += d;
        if (all < 10) {
            std::fprintf(stderr, "%s %.*s\n  want %.*s\n  got  %.*s\n", set.c_str(), static_cast<int>(header.size()),
                         header.data(), static_cast<int>(w.size()), w.data(), static_cast<int>(g.size()), g.data());
        }
        t.differing[k]++;
    }
}

}  // namespace

HD_TEST(pipeline_restore_golden) {
    const std::string dir = GoldenDir() + "/stages/";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        HD_SKIP(std::string("no ") + dir);
    }
    // The set is complete when its MANIFEST is written.
    HD_REQUIRE(std::filesystem::exists(dir + "MANIFEST", ec));
    std::vector<std::string> sets;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        const std::string suffix = ".R.tsv";
        if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        const std::string set = name.substr(0, name.size() - suffix.size());
        if (set.size() >= 4 && set.compare(set.size() - 4, 4, ".raw") == 0) continue;
        sets.push_back(set);
    }
    std::sort(sets.begin(), sets.end());
    HD_REQUIRE(!sets.empty());

    Totals t;
    for (const auto& set : sets) {
        std::string files[5];
        const char* stages[5] = {"P", "C", "R", "OUT", "CTX"};
        for (int s = 0; s < 5; s++) HD_REQUIRE(ReadFile(dir + set + "." + stages[s] + ".tsv", files[s]));
        std::vector<Block> blocks[5];
        for (int s = 0; s < 5; s++) {
            blocks[s] = Blocks(files[s]);
            HD_REQUIRE(blocks[s].size() == blocks[0].size());
        }
        std::string inFile;
        HD_REQUIRE(ReadFile(dir + set + ".IN.tsv", inFile));
        const std::vector<Block> inBlocks = Blocks(inFile);
        HD_REQUIRE(inBlocks.size() == blocks[0].size());
        for (size_t b = 0; b < blocks[0].size(); b++) {
            for (int s = 1; s < 5; s++) HD_REQUIRE(blocks[s][b].header == blocks[0][b].header);
            HD_REQUIRE(inBlocks[b].header == blocks[0][b].header);
            const std::string_view header = blocks[0][b].header;
            const auto& outRecords = blocks[3][b].records;
            if (outRecords.empty()) continue;  // a document header
            t.blocks++;
            std::string wholeLine;
            bool whole = false;
            for (const auto r : blocks[4][b].records) {
                const auto f = Split(r, '\t');
                if (f.size() == 3 && f[1] == "line.wholeline") {
                    wholeLine = UnescapeStageText(f[2]);
                    whole = true;
                }
            }
            const std::string in = InText(inBlocks[b].records);
            const bool cr = !in.empty() && in.back() == '\r';
            std::vector<std::string> got[4];
            if (whole) {
                t.wholeLines++;
                got[3].push_back("OUT\t" + EscapeStageText(wholeLine + (cr ? "\r" : "")));
            } else {
                const auto steps = pipeline::RestoreAll(ReadPieces(blocks[1][b].records, "C"),
                                                        ReadPieces(blocks[0][b].records, "P"), true);
                WriteRecords(got[0], "R1", steps.spaced);
                WriteRecords(got[1], "R2", steps.bracketed);
                WriteRecords(got[2], "R3", steps.korean);
                got[3].push_back("OUT\t" + EscapeStageText(steps.line + (cr ? "\r" : "")));
            }
            for (int k = 0; k < 4; k++) {
                const auto want = k < 3 ? Tagged(blocks[2][b].records, kTags[k]) : outRecords;
                Compare(set, header, k, got[k], want, t);
            }
        }
    }
    std::printf("pipeline_restore_golden: %zu sets, %zu lines (%zu whole), R1 %zu/%zu R2 %zu/%zu R3 %zu/%zu OUT %zu/%zu "
                "differing\n",
                sets.size(), t.blocks, t.wholeLines, t.differing[0], t.records[0], t.differing[1], t.records[1],
                t.differing[2], t.records[2], t.differing[3], t.records[3]);
    for (const size_t d : t.differing) HD_CHECK_EQ(d, static_cast<size_t>(0));
}
