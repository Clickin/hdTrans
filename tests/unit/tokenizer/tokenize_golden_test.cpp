// The tokenizer against the stage records of $HDTRANS_GOLDEN/stages: the T records of
// every set (tokens as scanned, before the per-token stage) and sweep.cp.tsv (the token
// types of あcあ for every code point c). Skipped when the set is absent.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/text/utf8.h"
#include "core/tokenizer/tokenize.h"
#include "hdtest.h"
#include "golden.h"
#include "stage_records.h"

using namespace hdTrans::tokenizer;

namespace {

// The T records of one chunk.
void AppendTRecords(const Tokens& t, int chunk, std::vector<std::string>& out) {
    for (size_t i = 0; i < t.size(); i++) {
        const std::string_view raw = t.Raw(i);
        std::string r = "T\t" + std::to_string(chunk) + "\t" + std::to_string(i) + "\t" + std::to_string(t[i].type) +
                        "\t" + std::to_string(t[i].span) + "\t" + stagetest::EscapeStageText(raw) + "\t";
        if (t[i].span != 0) r += stagetest::EscapeStageText(raw);
        out.push_back(std::move(r));
    }
}

// The T records of a set: from the IN records on the raw path (one text piece per
// line), from the CH records (the pieces of each chunk) on the text path.
std::vector<std::string> TRecords(std::string_view input, bool raw) {
    std::vector<std::string> out;
    Tokens tokens;
    std::vector<Piece> pieces;
    int chunk = -1;
    auto flush = [&] {
        if (chunk < 0) return;
        TokenizeSpans(pieces, true, tokens);
        AppendTRecords(tokens, chunk, out);
        pieces.clear();
        chunk = -1;
    };
    for (const std::string_view line : stagetest::Lines(input)) {
        if (line.starts_with("#")) {
            flush();
            out.emplace_back(line);
            continue;
        }
        const auto cols = stagetest::Columns(line);
        if (raw && cols[0] == "IN" && cols.size() == 2) {
            pieces.clear();
            if (!cols[1].empty()) pieces.push_back({stagetest::UnescapeStageText(cols[1]), 0});
            TokenizeSpans(pieces, false, tokens);
            AppendTRecords(tokens, 0, out);
        } else if (!raw && cols[0] == "CH" && cols.size() == 5) {
            const int k = std::atoi(std::string(cols[1]).c_str());
            if (k != chunk) flush();
            chunk = k;
            pieces.push_back({stagetest::UnescapeStageText(cols[4]), std::atoi(std::string(cols[3]).c_str())});
        }
    }
    flush();
    return out;
}

size_t Capacities(const Tokens& t) {
    return t.tokens.capacity() + t.markers.capacity() + t.text.capacity() + t.chars.capacity() +
           t.offsets.capacity() + t.spans.capacity() + t.buffer.capacity();
}

}  // namespace

HD_TEST(tokenizer_golden_t_records) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/stages/"));
    const std::string dir = stagetest::StagesDir();
    if (dir.empty()) {
        HD_SKIP("no complete stage set (HDTRANS_GOLDEN/stages/MANIFEST)");
    }
    std::vector<std::string> sets;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.size() > 6 && name.ends_with(".T.tsv")) sets.push_back(name.substr(0, name.size() - 6));
    }
    std::sort(sets.begin(), sets.end());
    HD_REQUIRE(!ec);
    HD_REQUIRE(!sets.empty());
    long long records = 0, differing = 0;
    size_t expectedRecords = 0;
    for (const std::string& set : sets) {
        const bool raw = set.ends_with(".raw");
        std::string input, want;
        if (!stagetest::ReadFile(dir + set + (raw ? ".IN.tsv" : ".P.tsv"), input) ||
            !stagetest::ReadFile(dir + set + ".T.tsv", want)) {
            hdtest::Fail(__FILE__, __LINE__, "cannot read the input or T records of " + set);
            continue;
        }
        expectedRecords += hdtest::CountRecords(want, "T");
        const std::vector<std::string> got = TRecords(input, raw);
        HD_CHECK_EQ(got.size(), hdtest::LineCount(want));
        const std::vector<std::string_view> wantLines = stagetest::Lines(want);
        long long setDiffering = 0;
        for (size_t i = 0; i < std::max(got.size(), wantLines.size()); i++) {
            const std::string_view g = i < got.size() ? std::string_view(got[i]) : "<none>";
            const std::string_view w = i < wantLines.size() ? wantLines[i] : "<none>";
            if (!w.starts_with("#")) records++;
            if (g == w) continue;
            if (++setDiffering <= 5) {
                hdtest::Fail(__FILE__, __LINE__,
                             set + " line " + std::to_string(i + 1) + ": got " + std::string(g) + " want " + std::string(w));
            }
        }
        differing += setDiffering;
    }
    std::printf("tokenizer_golden_t_records: %zu sets, %lld T records, %lld differing\n", sets.size(), records,
                differing);
    HD_CHECK(records > 0);
    HD_CHECK_EQ(static_cast<size_t>(records), expectedRecords);
    HD_CHECK_EQ(differing, 0LL);
}

HD_TEST(tokenizer_golden_sweep) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/stages/"));
    const std::string dir = stagetest::StagesDir();
    std::string sweep;
    if (dir.empty() || !stagetest::ReadFile(dir + "sweep.cp.tsv", sweep)) {
        HD_SKIP("no complete stage set with sweep.cp.tsv");
    }
    Tokens tokens;
    std::vector<Piece> pieces(1);
    std::string got;
    long long rows = 0, mismatches = 0;
    for (const std::string_view line : stagetest::Lines(sweep)) {
        const auto cols = stagetest::Columns(line);
        HD_REQUIRE(cols.size() == 4 && cols[0].starts_with("U+"));
        const auto c = static_cast<char32_t>(std::strtoul(std::string(cols[0].substr(2)).c_str(), nullptr, 16));
        pieces[0].text = "あ";
        hdTrans::text::AppendRune(pieces[0].text, c);
        pieces[0].text += "あ";
        rows++;
        for (int dashes = 0; dashes < 2; dashes++) {
            TokenizeSpans(pieces, dashes != 0, tokens);
            got.clear();
            for (size_t i = 0; i < tokens.size(); i++) {
                if (i > 0) got += ' ';
                got += std::to_string(tokens[i].type) + "/" + std::to_string(tokens[i].span);
            }
            if (got != cols[1 + dashes] && ++mismatches <= 5) {
                hdtest::Fail(__FILE__, __LINE__, std::string(cols[0]) + (dashes ? " text: " : " raw: ") + got +
                                                     " want " + std::string(cols[1 + dashes]));
            }
        }
    }
    std::printf("tokenizer_golden_sweep: %lld code points, %lld mismatches\n", rows, mismatches);
    HD_CHECK_EQ(static_cast<size_t>(rows), hdtest::LineCount(sweep));
    HD_CHECK_EQ(rows, 1112064LL);
    HD_CHECK_EQ(mismatches, 0LL);
}

// Tokenizing the chunks of corpus2: time, and no buffer of the Tokens grows once they
// have held every chunk (the tokenizer allocates nothing else).
HD_TEST(tokenizer_golden_speed) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/stages/"));
    const std::string dir = stagetest::StagesDir();
    std::string input;
    if (dir.empty() || !stagetest::ReadFile(dir + "corpus2.each.P.tsv", input)) {
        HD_SKIP("no corpus2.each.P.tsv");
    }
    std::vector<std::vector<Piece>> chunks;
    int chunk = -1;
    for (const std::string_view line : stagetest::Lines(input)) {
        const auto cols = stagetest::Columns(line);
        if (cols[0] != "CH" || cols.size() != 5) {
            chunk = -1;
            continue;
        }
        const int k = std::atoi(std::string(cols[1]).c_str());
        if (k != chunk) chunks.emplace_back();
        chunk = k;
        chunks.back().push_back({stagetest::UnescapeStageText(cols[4]), std::atoi(std::string(cols[3]).c_str())});
    }
    HD_CHECK_EQ(chunks.size(), size_t{43176});
    std::string expectedTokens;
    HD_REQUIRE(stagetest::ReadFile(dir + "corpus2.each.T.tsv", expectedTokens));
    Tokens tokens;
    size_t count = 0;
    for (const auto& c : chunks) TokenizeSpans(c, true, tokens);  // grow the buffers
    const size_t capacities = Capacities(tokens);
    const auto start = std::chrono::steady_clock::now();
    for (const auto& c : chunks) {
        TokenizeSpans(c, true, tokens);
        count += tokens.size();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("tokenizer_golden_speed: %zu chunks, %zu tokens, %.3f s\n", chunks.size(), count, seconds);
    HD_CHECK_EQ(Capacities(tokens), capacities);
    HD_CHECK_EQ(count, hdtest::CountRecords(expectedTokens, "T"));
}
