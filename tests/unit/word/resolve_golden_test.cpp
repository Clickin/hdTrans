// The end-of-sentence passes against $HDTRANS_GOLDEN/stages/<set>.S.tsv: per chunk of the
// analyze fixtures, the item chain of each sentence after Flush, written as S records. The
// rule program is $HDTRANS_GOLDEN/rules/rules.flat. Skipped when the fixtures or the rule
// program are absent.
#include <algorithm>
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
#include "hdTrans/output/families.h"
#include "hdTrans/rules/rules.h"
#include "hdTrans/tokenizer/tokenize.h"
#include "hdTrans/word/analyze.h"
#include "hdTrans/word/flush.h"
#include "hdTrans/word/worker.h"
#include "hdtest.h"
#include "tokenizer/stage_records.h"
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

const dict::Snapshot* Dictionaries() {
    static std::shared_ptr<const dict::Snapshot> snapshot = [] {
        dict::LoadResult r = dict::LoadFolders({DictsDir()});
        return r.snapshot;
    }();
    return snapshot.get();
}

const rules::Program* RuleProgram() {
    static std::unique_ptr<rules::Program> program = [] {
        std::string text;
        if (!stagetest::ReadFile(GoldenDir() + "/rules/rules.flat", text)) return std::unique_ptr<rules::Program>();
        auto p = std::make_unique<rules::Program>();
        rules::LoadError error{};
        if (!rules::Load(text, *p, error)) return std::unique_ptr<rules::Program>();
        return p;
    }();
    return program.get();
}

// The S lines of the expected stage file by chunk: the key is the header the analyze
// fixtures use for the chunk (the #CASE line, with the #LINE line of a document) and
// the chunk number, the first column.
std::map<std::string, std::vector<std::string>> ExpectedS(const std::string& path) {
    std::map<std::string, std::vector<std::string>> out;
    std::string data;
    if (!stagetest::ReadFile(path, data)) return out;
    std::string caseLine, header;
    for (const std::string_view line : stagetest::Lines(data)) {
        if (line.starts_with("#CASE")) {
            caseLine = header = std::string(line);
        } else if (line.starts_with("#LINE")) {
            header = caseLine + "|" + std::string(line);
        } else if (line.starts_with("S\t")) {
            const auto f = stagetest::Columns(line);
            out[header + "\t" + std::string(f[1])].emplace_back(line);
        }
    }
    return out;
}

std::string Pos(int token, int offset) { return std::to_string(token) + ":" + std::to_string(offset); }

std::string Esc(std::string_view s) { return stagetest::EscapeStageText(s); }

std::string PartCode(const word::Entry& e) {
    return e.cls == 0 ? std::string(e.code) : std::string(1, e.cls) + std::string(e.code);
}

const char* DetailName(int cls) {
    static const char* const names[] = {"TWordInfo", "TVerbWord", "TKeiyoWord", "TDakeiyoWord"};
    return names[cls];
}

// U+AC00..U+D7A3 in s.
bool HasHangul(std::string_view s) {
    for (std::size_t i = 0; i + 2 < s.size(); i++) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        if (b0 < 0xE0 || b0 > 0xEF) continue;
        const auto b1 = static_cast<unsigned char>(s[i + 1]);
        const auto b2 = static_cast<unsigned char>(s[i + 2]);
        const unsigned cp = ((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
        if (cp >= 0xAC00 && cp <= 0xD7A3) return true;
    }
    return false;
}

// The S record of item it in sentence sent of chunk chunk (the columns of the stage
// file after the tag).
std::string SRecord(const word::Item& it, int chunk, int sent) {
    std::vector<std::string> c;
    c.push_back(std::to_string(chunk));
    c.push_back(std::to_string(sent));
    c.push_back(Pos(it.tokenIndex, it.start));
    c.push_back(std::to_string(it.length));
    c.push_back(Esc(it.group->surface));
    c.push_back(std::to_string(it.selected));
    c.push_back(std::to_string(it.selectionFlag));
    c.push_back(DetailName(it.detail->cls));
    c.push_back(std::to_string(it.DetailKind()));
    c.push_back(std::to_string(it.DetailRecord()));
    c.push_back(std::to_string(it.DirectIndex()));
    const word::Entry* sel = it.group->At(it.selected);
    if (sel != nullptr) {
        c.push_back(Esc(PartCode(*sel)));
        c.push_back(Esc(sel->attr));
        c.push_back(Esc(sel->gattr));
        c.push_back(Esc(sel->dst));
    } else {
        c.insert(c.end(), 4, "-");
    }
    const word::Entry* end = it.DirectEntry(it.DirectIndex());
    if (it.DirectIndex() >= 0 && end != nullptr) {
        c.push_back(Esc(PartCode(*end)));
        c.push_back(Esc(end->gattr));
        c.push_back(Esc(end->dst));
    } else {
        c.insert(c.end(), 3, "-");
    }
    // compose fields
    if (!it.group->entries.empty()) {
        const word::Entry& e0 = *it.group->entries[0];
        c.push_back(Esc(PartCode(e0)));
        c.push_back(Esc(e0.gattr));
        c.push_back(Esc(e0.dst));
        c.push_back(e0.fixed ? "1" : "0");
    } else {
        c.insert(c.end(), 3, "-");
        c.push_back("0");
    }
    c.push_back(Esc(it.DirectSurface()));
    c.push_back(std::to_string(it.DirectLength()));
    c.push_back(it.DirectCount() > 0 && it.DirectEntry(0) != nullptr && it.DirectEntry(0)->user != nullptr ? "1" : "0");
    bool other = false;
    for (int i = 0; !it.Code(i).empty() || !it.EntryText(i).empty(); i++) {
        if (i == 0 || i == it.selected) continue;
        if (HasHangul(it.EntryText(i))) other = true;
    }
    c.push_back(other ? "1" : "0");
    std::string line = "S";
    for (const std::string& col : c) {
        line += '\t';
        line += col;
    }
    return line;
}

struct Totals {
    long long chunks = 0, records = 0, differing = 0;
    double seconds = 0;
};

void RunSet(const std::string& dir, const std::string& set, analyzetest::Environments& envs, Totals& totals) {
    std::string data;
    if (!analyzetest::ReadFile(dir + set + ".tsv", data)) {
        hdtest::Fail(__FILE__, __LINE__, "cannot read " + set);
        return;
    }
    const auto expected = ExpectedS(GoldenDir() + "/stages/" + set + ".S.tsv");
    const dict::Snapshot* snapshot = Dictionaries();
    const rules::Program& program = *RuleProgram();
    const output::Families families;
    tokenizer::Tokens tokens;
    word::Arena arena;
    word::Analysis analysis;
    analyzetest::Chunk chunk;
    int shown = 0;
    auto finish = [&] {
        if (!chunk.active) return;
        chunk.active = false;
        const analyzetest::Environment* env = envs.Get(chunk.env);
        if (env == nullptr) {
            hdtest::Fail(__FILE__, __LINE__, set + ": no environment " + std::to_string(chunk.env));
            return;
        }
        tokenizer::TokenizeSpans(chunk.pieces, env->settings.text, tokens);
        const auto start = std::chrono::steady_clock::now();
        std::vector<std::string> got;
        std::vector<std::string> sink;
        analyzetest::Analyze(tokens, *snapshot, *env, chunk.index, chunk.faultSentence, arena, analysis, sink);
        word::Worker& w = *analysis.worker;
        w.settings.terminal = env->settings.terminal;
        for (std::size_t s = 0; s < analysis.sentences.size(); s++) {
            word::Sentence& sentence = analysis.sentences[s];
            w.chain = sentence.chain;
            w.current = sentence.current;
            w.transformSurface = sentence.transformSurface;
            word::Flusher flusher{w, program, families};
            word::Flush(flusher);
            for (const word::Item* it = w.chain.head; it != nullptr; it = it->next) {
                got.push_back(SRecord(*it, chunk.index, static_cast<int>(s)));
            }
            sentence.chain = w.chain;
        }
        totals.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        totals.chunks++;
        const auto found = expected.find(chunk.header + "\t" + std::to_string(chunk.index));
        const std::vector<std::string> want = found == expected.end() ? std::vector<std::string>{} : found->second;
        const size_t n = std::max(got.size(), want.size());
        for (size_t i = 0; i < n; i++) {
            totals.records++;
            const std::string_view g = i < got.size() ? std::string_view(got[i]) : "<none>";
            const std::string_view wv = i < want.size() ? std::string_view(want[i]) : "<none>";
            if (g == wv) continue;
            totals.differing++;
            if (++shown <= 8) {
                hdtest::Fail(__FILE__, __LINE__, set + " " + chunk.header + " record " + std::to_string(i) + "\n  got  " +
                                                     std::string(g) + "\n  want " + std::string(wv));
            }
        }
    };
    analyzetest::ForEachChunk(data, chunk, finish);
}

// The text-path sets run with HDTRANS_RESOLVE_TEXT set until their passes are in.
void RunSets(const char* name, bool (*select)(const std::string&), bool textPath) {
    const std::string dir = GoldenDir() + "/analyze/";
    if (!std::filesystem::exists(dir + "MANIFEST") || Dictionaries() == nullptr || RuleProgram() == nullptr) {
        std::printf("skip %s: no analyze fixtures, dictionaries or rule program\n", name);
        return;
    }
    if (textPath && std::getenv("HDTRANS_RESOLVE_TEXT") == nullptr) {
        std::printf("skip %s: text-path passes pending (set HDTRANS_RESOLVE_TEXT to run)\n", name);
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
    std::printf("%s: %zu sets, %lld chunks, %lld S records, %lld differing, %.2f s\n", name, sets.size(), totals.chunks,
                totals.records, totals.differing, totals.seconds);
    HD_CHECK_EQ(totals.differing, 0LL);
}

}  // namespace

HD_TEST(resolve_golden_raw) {
    RunSets("resolve_golden_raw", [](const std::string& s) { return s == "corpus.raw"; }, false);
}

HD_TEST(resolve_golden_each) {
    RunSets("resolve_golden_each", [](const std::string& s) { return s == "corpus2.each"; }, true);
}

HD_TEST(resolve_golden_docs) {
    RunSets("resolve_golden_docs", [](const std::string& s) { return s.starts_with("doc-"); }, true);
}
