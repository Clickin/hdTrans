// The driver against the stage records of the expectation sets: every line of a set is
// translated by the driver with the stage observer, and the records it makes are compared
// with $HDTRANS_GOLDEN/stages/<set>.<TAG>.tsv: N, SN, P (with CH), T and L (the word
// analysis, before resolve) must be equal; S (the chain after resolve) is compared per
// column and printed. Sets: corpus.raw (the raw path), corpus2.each (a session per line)
// and the doc-* and x-* sets (one document each). Dictionaries: $HDTRANS_DICTS (default
// $HDTRANS_GOLDEN/dicts/base). Skipped when the stages MANIFEST is absent.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/engine/driver.h"
#include "hdTrans/engine/glue.h"
#include "hdtest.h"
#include "output/stage_records.h"
#include "word/analyze_records.h"

using namespace hdTrans;
using namespace stagerecords;

namespace {

std::string GoldenRoot() {
    if (const char* g = std::getenv("HDTRANS_GOLDEN"); g != nullptr && *g != 0) return g;
    std::string f = __FILE__;
    const size_t slash = f.find_last_of("/\\");
    return (slash == std::string::npos ? std::string(".") : f.substr(0, slash)) + "/../../../../hdtrans-golden";
}

std::string DictsDir() {
    if (const char* d = std::getenv("HDTRANS_DICTS"); d != nullptr && *d != 0) return d;
    return GoldenRoot() + "/dicts/base";
}

const dict::Snapshot* Dictionaries() {
    static std::shared_ptr<const dict::Snapshot> snapshot = [] {
        dict::LoadResult r = dict::LoadFolders({DictsDir()});
        return r.snapshot;
    }();
    return snapshot.get();
}

// The records of one run, by stage file and by line (the number of a #LINE or #CASE header).
struct Collector {
    std::map<std::string, std::map<int, std::vector<std::string>>> records;
    int key = 0;
    int caseBase = 0;  // the case number before the line of a call

    void Add(std::string_view file, std::string record) {
        records[std::string(file)][key].push_back(std::move(record));
    }
};

std::string Esc(std::string_view s) { return EscapeStageText(s); }

void OnLine(void* context, int line) {
    auto* c = static_cast<Collector*>(context);
    c->key = c->caseBase + line + 1;
}

void OnText(void* context, std::string_view tag, std::string_view text) {
    static_cast<Collector*>(context)->Add(tag, std::string(tag) + "\t" + Esc(text));
}

void OnPieces(void* context, std::string_view tag, int chunk, const output::Pieces& pieces) {
    auto* c = static_cast<Collector*>(context);
    (void)tag;
    for (size_t i = 0; i < pieces.size(); i++) {
        const std::string span = std::to_string(static_cast<int>(pieces[i].span));
        if (chunk < 0) {
            c->Add("P", "P\t" + std::to_string(i) + "\t" + span + "\t" + Esc(pieces[i].text));
        } else {
            c->Add("P", "CH\t" + std::to_string(chunk) + "\t" + std::to_string(i) + "\t" + span + "\t" + Esc(pieces[i].text));
        }
    }
}

void OnTokens(void* context, int chunk, const tokenizer::Tokens& tokens) {
    auto* c = static_cast<Collector*>(context);
    for (size_t i = 0; i < tokens.size(); i++) {
        const std::string_view raw = tokens.Raw(i);
        std::string r = "T\t" + std::to_string(chunk) + "\t" + std::to_string(i) + "\t" +
                        std::to_string(tokens[i].type) + "\t" + std::to_string(tokens[i].span) + "\t" + Esc(raw) + "\t";
        if (tokens[i].span != 0) r += Esc(raw);
        c->Add("T", std::move(r));
    }
}

void OnAnalysis(void* context, int chunk, const tokenizer::Tokens& tokens, const word::Analysis& analysis) {
    auto* c = static_cast<Collector*>(context);
    std::vector<std::string> out;
    for (size_t s = 0; s < analysis.sentences.size(); s++) {
        out.clear();
        analyzetest::SentenceRecords(tokens, analysis.sentences[s], chunk, static_cast<int>(s), out);
        for (std::string& r : out) {
            if (!r.starts_with("LX\t")) c->Add("L", std::move(r));
        }
    }
}

// The S record of an item in the reference's column order, with the columns this engine
// does not carry (entry attributes) written as "-".
std::string SRecord(int chunk, int sent, const output::ResolvedItem& it) {
    static const char* const kDetail[] = {"TWordInfo", "TVerbWord", "TKeiyoWord", "TDakeiyoWord"};
    auto part = [](const output::ResolvedEntry& e, bool present) {
        return present ? Esc(e.code) : std::string("-");
    };
    const bool hasEntry = !it.entry.code.empty() || !it.entry.text.empty() || !it.entry.gattr.empty();
    const bool hasEnding = !it.ending.code.empty() || !it.ending.text.empty() || !it.ending.gattr.empty();
    const bool hasFirst = !it.first.code.empty() || !it.first.text.empty() || !it.first.gattr.empty();
    std::vector<std::string> c = {
        "S", std::to_string(chunk), std::to_string(sent), std::to_string(it.tokenIndex) + ":" + std::to_string(it.offset),
        std::to_string(it.length), Esc(it.surface), std::to_string(it.selected), std::to_string(it.selectionFlag),
        kDetail[static_cast<int>(it.detailClass)], std::to_string(it.detailKind), std::to_string(it.detailRecord),
        std::to_string(it.directIndex),
        part(it.entry, hasEntry), "-", hasEntry ? Esc(it.entry.gattr) : "-", hasEntry ? Esc(it.entry.text) : "-",
        part(it.ending, hasEnding), hasEnding ? Esc(it.ending.gattr) : "-", hasEnding ? Esc(it.ending.text) : "-",
        part(it.first, hasFirst), hasFirst ? Esc(it.first.gattr) : "-", hasFirst ? Esc(it.first.text) : "-",
        it.fixed ? "1" : "0", Esc(it.endingSurface), std::to_string(it.endingLength), it.ownedEnding ? "1" : "0",
        it.otherHangul ? "1" : "0",
    };
    std::string out;
    for (size_t i = 0; i < c.size(); i++) {
        if (i > 0) out += '\t';
        out += c[i];
    }
    return out;
}

void OnResolved(void* context, int chunk, int sentence, const word::Chain& chain) {
    auto* c = static_cast<Collector*>(context);
    std::vector<output::ResolvedItem> items;
    internal::ResolvedItems(chain, items);
    for (const output::ResolvedItem& it : items) c->Add("S", SRecord(chunk, sentence, it));
}

internal::StageObserver ObserverFor(Collector& c) {
    internal::StageObserver o;
    o.context = &c;
    o.line = OnLine;
    o.text = OnText;
    o.pieces = OnPieces;
    o.tokens = OnTokens;
    o.analysis = OnAnalysis;
    o.resolved = OnResolved;
    return o;
}

// Golden records by file and by line number.
using Golden = std::map<std::string, std::map<int, std::vector<std::string>>>;

// The number of a block header: #LINE k or #CASE k; -1 for a header without one.
int HeaderKey(std::string_view header) {
    const auto f = Split(header, '\t');
    if (f.size() < 2) return -1;
    if (f[0] != "#LINE" && f[0] != "#CASE") return -1;
    if (f[1].empty() || !std::all_of(f[1].begin(), f[1].end(), [](char ch) { return ch >= '0' && ch <= '9'; })) return -1;
    return std::atoi(std::string(f[1]).c_str());
}

bool LoadGolden(const std::string& set, const std::vector<const char*>& tags, Golden& golden) {
    const std::string dir = GoldenRoot() + "/stages/";
    for (const char* tag : tags) {
        std::string text;
        if (!ReadFile(dir + set + "." + tag + ".tsv", text)) return false;
        for (const Block& b : Blocks(text)) {
            const int key = HeaderKey(b.header);
            if (key < 0) continue;
            auto& list = golden[tag][key];
            for (const auto r : b.records) list.push_back(std::string(r));
        }
    }
    return true;
}

// The S columns compared; column 13 (entry attributes) is not carried by the engine.
bool SColumnCompared(size_t i) { return i != 13; }

// Records that differ between got and want, by position; for S, by compared column.
long long Differing(const std::string& tag, const std::vector<std::string>& got, const std::vector<std::string>& want,
                    std::map<size_t, long long>& columns) {
    long long differing = 0;
    const size_t n = std::max(got.size(), want.size());
    for (size_t i = 0; i < n; i++) {
        const std::string_view g = i < got.size() ? std::string_view(got[i]) : std::string_view("<none>");
        const std::string_view w = i < want.size() ? std::string_view(want[i]) : std::string_view("<none>");
        if (tag != "S") {
            if (g != w) differing++;
            continue;
        }
        const auto gf = Split(g, '\t');
        const auto wf = Split(w, '\t');
        bool any = gf.size() != wf.size();
        for (size_t c = 0; c < std::min(gf.size(), wf.size()) && c < 27; c++) {
            if (!SColumnCompared(c)) continue;
            std::string_view a = gf[c], b = wf[c];
            // The part code: the golden record carries the major tag.
            if ((c == 12 || c == 16 || c == 19) && b != "-" && !b.empty()) b.remove_prefix(1);
            if (a != b) {
                columns[c]++;
                any = true;
            }
        }
        if (any) differing++;
    }
    return differing;
}

struct Totals {
    std::map<std::string, long long> records, differing;
};

// Runs one set: every line through the driver, then the records against the stages.
void RunSet(const std::string& set, const std::vector<std::string>& lines, bool raw, Totals& totals) {
    const std::vector<const char*> tags = raw ? std::vector<const char*>{"T", "L", "S"}
                                              : std::vector<const char*>{"N", "SN", "P", "T", "L", "S"};
    Golden golden;
    if (!LoadGolden(set, tags, golden)) {
        hdtest::Fail(__FILE__, __LINE__, "no stage records of " + set);
        return;
    }
    Collector c;
    const internal::StageObserver observer = ObserverFor(c);
    const dict::Snapshot& snap = *Dictionaries();
    const bool each = set.ends_with(".each") || raw;
    internal::Generated generated;
    if (!each) {
        c.caseBase = 0;
        internal::Memory memory;
        internal::TranslateDocument(snap, generated, lines, memory, observer);
    } else {
        for (size_t i = 0; i < lines.size(); i++) {
            c.caseBase = static_cast<int>(i);
            if (raw) {
                internal::TranslateRawLine(snap, generated, lines[i], observer);
            } else {
                internal::Memory fresh;
                internal::TranslateDocument(snap, generated, std::vector<std::string>{lines[i]}, fresh, observer);
            }
        }
    }
    if (const char* dump = std::getenv("HDTRANS_DRIVER_DUMP"); dump != nullptr && *dump != 0) {
        for (const char* tag : tags) {
            std::FILE* f = std::fopen((std::string(dump) + "/" + set + "." + tag + ".got.tsv").c_str(), "wb");
            if (f == nullptr) continue;
            for (const auto& [k, recs] : c.records[tag]) {
                std::fprintf(f, "#LINE\t%d\n", k);
                for (const std::string& r : recs) std::fprintf(f, "%s\n", r.c_str());
            }
            std::fclose(f);
        }
    }
    for (const char* tag : tags) {
        std::map<size_t, long long> columns;
        long long differing = 0, records = 0;
        const auto& got = c.records[tag];
        const auto& want = golden[tag];
        std::vector<int> keys;
        for (const auto& [k, v] : want) keys.push_back(k);
        for (const auto& [k, v] : got) keys.push_back(k);
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        const std::vector<std::string> none;
        for (const int k : keys) {
            const auto gi = got.find(k);
            const auto wi = want.find(k);
            const auto& g = gi == got.end() ? none : gi->second;
            const auto& w = wi == want.end() ? none : wi->second;
            records += static_cast<long long>(w.size());
            differing += Differing(tag, g, w, columns);
        }
        totals.records[tag] += records;
        totals.differing[tag] += differing;
        if (std::string_view(tag) == "S" && differing > 0) {
            std::printf("  S columns differing in %s:", set.c_str());
            for (const auto& [col, n] : columns) std::printf(" %zu:%lld", col, n);
            std::printf("\n");
        }
    }
    std::printf("driver_golden %s:", set.c_str());
    for (const char* tag : tags) std::printf(" %s %lld/%lld", tag, totals.differing[tag], totals.records[tag]);
    std::printf("\n");
}

std::vector<std::string> SplitLines(const std::string& text, bool keepTrailing) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            if (keepTrailing || start < text.size()) out.push_back(text.substr(start));
            break;
        }
        out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

}  // namespace

HD_TEST(engine_driver_golden) {
    const std::string golden = GoldenRoot();
    std::error_code ec;
    if (!std::filesystem::exists(golden + "/stages/MANIFEST", ec) || Dictionaries() == nullptr) {
        std::printf("skip engine_driver_golden: no %s/stages/MANIFEST or dictionaries\n", golden.c_str());
        return;
    }
    std::vector<std::string> sets;
    for (std::filesystem::directory_iterator it(golden, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.size() > 3 && name.ends_with(".in")) {
            const std::string set = name.substr(0, name.size() - 3);
            sets.push_back(set);
        }
    }
    std::sort(sets.begin(), sets.end());
    HD_REQUIRE(!sets.empty());

    Totals totals;
    for (const std::string& set : sets) {
        if (set != "corpus2.each" && !set.starts_with("doc-") && !set.starts_with("x-") && set != "corpus.raw") continue;
        std::string text;
        if (!ReadFile(golden + "/" + set + ".in", text)) continue;
        const bool raw = set == "corpus.raw";
        const bool each = set == "corpus2.each";
        std::vector<std::string> lines = SplitLines(text, !raw && !each);
        if (raw || each) {
            // The CLI reads a line per session and skips the empty tail.
            while (!lines.empty() && lines.back().empty()) lines.pop_back();
        }
        RunSet(set, lines, raw, totals);
    }
    long long gated = 0;
    for (const char* tag : {"N", "SN", "P", "T", "L"}) gated += totals.differing[tag];
    std::printf("driver_golden: stages up to analyze differing %lld; S differing %lld of %lld\n", gated,
                totals.differing["S"], totals.records["S"]);
    HD_CHECK_EQ(gated, 0LL);
}
