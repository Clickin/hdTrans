// The driver against the stage records of the expectation sets. Every line of a set is
// translated by the driver with the stage observer; the records it makes are compared
// with $HDTRANS_GOLDEN/stages/<set>.<TAG>.tsv, by record tag and line.
// All observed stages are gated. A line that differs is also counted under its first
// differing stage, so a cause shows in the stage it starts in.
// Sets: corpus.raw (the raw path: T, L, S, K), corpus2.each (a session per line) and the
// doc-* and x-* sets (one document each). Dictionaries: $HDTRANS_DICTS.
// Skipped when the stages MANIFEST is absent. With
// HDTRANS_DRIVER_DUMP set to a directory, the records made are written there as
// <set>.<TAG>.got.tsv.
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
#include "golden.h"
#include "output/stage_records.h"
#include "word/analyze_records.h"

using namespace hdTrans;
using namespace stagerecords;

namespace {

using hdtest::GoldenDir;

using hdtest::DictsDir;

const dict::Snapshot* Dictionaries() {
    static std::shared_ptr<const dict::Snapshot> snapshot = [] {
        dict::LoadResult r = dict::LoadFolders({DictsDir()});
        return r.snapshot;
    }();
    return snapshot.get();
}

// Records by their tag (the first column) and by line (the number of a #LINE or #CASE header).
using Records = std::map<std::string, std::map<int, std::vector<std::string>>>;

struct Collector {
    Records records;
    int key = 0;
    int caseBase = 0;          // the case number before the line of a call
    std::vector<bool> crs;     // per line of the call: the line ended in "\r"
    bool cr = false;           // the line being translated ends in "\r"

    void Add(std::string record) {
        std::string tag = record.substr(0, record.find('\t'));
        records[tag][key].push_back(std::move(record));
    }
};

std::string Esc(std::string_view s) { return EscapeStageText(s); }

void OnLine(void* context, int line) {
    auto* c = static_cast<Collector*>(context);
    c->key = c->caseBase + line + 1;
    c->cr = static_cast<size_t>(line) < c->crs.size() && c->crs[static_cast<size_t>(line)];
}

void OnText(void* context, std::string_view tag, std::string_view text) {
    static_cast<Collector*>(context)->Add(std::string(tag) + "\t" + Esc(text));
}

void OnPieces(void* context, std::string_view tag, int chunk, const output::Pieces& pieces) {
    auto* c = static_cast<Collector*>(context);
    (void)tag;
    for (size_t i = 0; i < pieces.size(); i++) {
        const std::string span = std::to_string(static_cast<int>(pieces[i].span));
        if (chunk < 0) {
            c->Add("P\t" + std::to_string(i) + "\t" + span + "\t" + Esc(pieces[i].text));
        } else {
            c->Add("CH\t" + std::to_string(chunk) + "\t" + std::to_string(i) + "\t" + span + "\t" + Esc(pieces[i].text));
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
        c->Add(std::move(r));
    }
}

void OnAnalysis(void* context, int chunk, const tokenizer::Tokens& tokens, const word::Analysis& analysis) {
    auto* c = static_cast<Collector*>(context);
    std::vector<std::string> out;
    for (size_t s = 0; s < analysis.sentences.size(); s++) {
        out.clear();
        analyzetest::SentenceRecords(tokens, analysis.sentences[s], chunk, static_cast<int>(s), out);
        for (std::string& r : out) {
            if (!r.starts_with("LX\t")) c->Add(std::move(r));
        }
    }
}

// The S record of an item in the reference's column order. The engine does not carry the
// semantic codes of an entry (column 13), written as "-".
std::string SRecord(int chunk, int sent, const output::ResolvedItem& it) {
    static const char* const kDetail[] = {"TWordInfo", "TVerbWord", "TKeiyoWord", "TDakeiyoWord"};
    auto part = [](const output::ResolvedEntry& e, bool present) {
        return present ? Esc(e.code) : std::string("-");
    };
    const bool hasEntry = !it.entry.code.empty() || !it.entry.text.empty() || !it.entry.gattr.empty();
    const bool hasEnding = !it.ending.code.empty() || !it.ending.text.empty() || !it.ending.gattr.empty();
    const bool hasFirst = !it.first.code.empty() || !it.first.text.empty() || !it.first.gattr.empty();
    const std::string columns[] = {
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
    for (size_t i = 0; i < std::size(columns); i++) {
        if (i > 0) out += '\t';
        out += columns[i];
    }
    return out;
}

void OnResolved(void* context, int chunk, int sentence, const word::Chain& chain) {
    auto* c = static_cast<Collector*>(context);
    std::vector<output::ResolvedItem> items;
    internal::ResolvedItems(chain, items);
    for (const output::ResolvedItem& it : items) c->Add(SRecord(chunk, sentence, it));
}

void OnComposed(void* context, int chunk, int sentence, const output::Pieces& pieces) {
    auto* c = static_cast<Collector*>(context);
    for (size_t p = 0; p < pieces.size(); p++) {
        c->Add("K\t" + std::to_string(chunk) + "\t" + std::to_string(sentence) + "\t" + std::to_string(p) + "\t" +
               std::to_string(static_cast<int>(pieces[p].span)) + "\t" + Esc(pieces[p].text));
    }
}

void OnCleaned(void* context, const output::Pieces& pieces) {
    auto* c = static_cast<Collector*>(context);
    for (size_t p = 0; p < pieces.size(); p++) {
        c->Add("C\t" + std::to_string(p) + "\t" + std::to_string(static_cast<int>(pieces[p].span)) + "\t" +
               Esc(pieces[p].text));
    }
}

// The pieces of one restore step, as the stage file writes them.
void AddPieces(Collector& c, const char* tag, const output::Pieces& pieces) {
    for (size_t p = 0; p < pieces.size(); p++) {
        c.Add(std::string(tag) + "\t" + std::to_string(p) + "\t" + std::to_string(static_cast<int>(pieces[p].span)) +
              "\t" + Esc(pieces[p].text));
    }
}

void OnRestored(void* context, const pipeline::RestoreSteps& steps) {
    auto* c = static_cast<Collector*>(context);
    AddPieces(*c, "R1", steps.spaced);
    AddPieces(*c, "R2", steps.bracketed);
    AddPieces(*c, "R3", steps.korean);
    c->Add("OUT\t" + Esc(steps.line + (c->cr ? "\r" : "")));
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
    o.composed = OnComposed;
    o.cleaned = OnCleaned;
    o.restored = OnRestored;
    return o;
}

// The number of a block header: #LINE k or #CASE k; -1 for a header without one.
int HeaderKey(std::string_view header) {
    const auto f = Split(header, '\t');
    if (f.size() < 2) return -1;
    if (f[0] != "#LINE" && f[0] != "#CASE") return -1;
    if (f[1].empty() || !std::all_of(f[1].begin(), f[1].end(), [](char ch) { return ch >= '0' && ch <= '9'; })) return -1;
    return std::atoi(std::string(f[1]).c_str());
}

// The golden records of the stage files of a set, by record tag and line. K0 (token texts
// of a sentence) and the lookup records are not compared.
Records LoadGolden(const std::string& set, const std::vector<const char*>& files) {
    Records golden;
    const std::string dir = GoldenDir() + "/stages/";
    for (const char* file : files) {
        std::string text;
        if (!ReadFile(dir + set + "." + file + ".tsv", text)) {
            hdtest::Fail(__FILE__, __LINE__, "missing stage file: " + set + "." + file);
            return {};
        }
        for (const Block& b : Blocks(text)) {
            const int key = HeaderKey(b.header);
            if (key < 0) continue;
            for (const auto r : b.records) {
                const std::string tag(r.substr(0, r.find('\t')));
                if (tag == "K0" || tag == "LK" || tag == "LKC") continue;
                golden[tag][key].push_back(std::string(r));
            }
        }
        for (const auto& [tag, expected] : hdtest::RecordCounts(text)) {
            if (tag == "K0" || tag == "LK" || tag == "LKC") continue;
            size_t loaded = 0;
            for (const auto& [key, records] : golden[tag]) loaded += records.size();
            HD_CHECK_EQ(loaded, expected);
        }
    }
    return golden;
}

// Whether the records of one tag in a line differ. For S, the compared columns decide, and
// each column that differs is counted in columns. Column 13 (the entry's semantic codes) is
// not carried by the engine.
bool Differs(const std::string& tag, const std::vector<std::string>& got, const std::vector<std::string>& want,
             std::map<size_t, long long>& columns) {
    if (got.size() != want.size()) return true;
    bool any = false;
    for (size_t i = 0; i < got.size(); i++) {
        if (tag != "S") {
            if (got[i] != want[i]) any = true;
            continue;
        }
        const auto gf = Split(got[i], '\t');
        const auto wf = Split(want[i], '\t');
        if (gf.size() != 27 || wf.size() != 27) return true;
        for (size_t c = 0; c < std::min(gf.size(), wf.size()) && c < 27; c++) {
            if (c == 13) continue;
            std::string_view a = gf[c], b = wf[c];
            // The part code: the reference's record carries its major tag.
            if ((c == 12 || c == 16 || c == 19) && b != "-" && !b.empty()) b.remove_prefix(1);
            if (a != b) {
                columns[c]++;
                any = true;
            }
        }
    }
    return any;
}

// The stages checked per line, in the order a first difference is assigned to.
const std::vector<std::string> kLineStages = {"S", "K", "C", "R1", "R2", "R3", "OUT"};

struct Totals {
    std::map<std::string, long long> records, differing;
    std::map<std::string, long long> firstStage;          // lines by their first differing stage
    long long lines = 0, identical = 0;
    std::map<size_t, long long> sColumns;
};

// Writes the records of a run to HDTRANS_DRIVER_DUMP as <set>.<tag>.got.tsv.
void Dump(const std::string& set, const Collector& c) {
    const char* dump = std::getenv("HDTRANS_DRIVER_DUMP");
    if (dump == nullptr || *dump == 0) return;
    for (const auto& [tag, byLine] : c.records) {
        std::FILE* f = std::fopen((std::string(dump) + "/" + set + "." + tag + ".got.tsv").c_str(), "wb");
        if (f == nullptr) continue;
        for (const auto& [k, recs] : byLine) {
            std::fprintf(f, "#LINE\t%d\n", k);
            for (const std::string& r : recs) std::fprintf(f, "%s\n", r.c_str());
        }
        std::fclose(f);
    }
}

// Runs one set: every line through the driver, then the records against the golden ones.
void RunSet(const std::string& set, const std::vector<std::string>& lines, bool raw, Totals& totals) {
    const std::vector<const char*> files = raw ? std::vector<const char*>{"T", "L", "S", "K"}
                                               : std::vector<const char*>{"N", "SN", "P", "T", "L", "S", "K", "C", "R", "OUT"};
    const Records golden = LoadGolden(set, files);
    if (golden.empty()) {
        hdtest::Fail(__FILE__, __LINE__, "no stage records of " + set);
        return;
    }
    internal::TranslationOptions options;
    options.reference = true;
    Collector c;
    const internal::StageObserver observer = ObserverFor(c);
    const dict::Snapshot& snap = *Dictionaries();
    const bool each = set.ends_with(".each") || raw;
    const std::shared_ptr<internal::Generated> generated = internal::NewGenerated(snap);
    std::vector<bool> crs;
    for (const std::string& line : lines) crs.push_back(!line.empty() && line.back() == '\r');
    if (!each) {
        internal::Memory memory;
        c.crs = crs;
        internal::TranslateDocument(snap, *generated, lines, memory, observer, options);
    } else {
        for (size_t i = 0; i < lines.size(); i++) {
            c.caseBase = static_cast<int>(i);
            c.crs = {crs[i]};
            if (raw) {
                internal::TranslateRawLine(snap, *generated, lines[i], observer, options);
            } else {
                internal::Memory fresh;
                internal::TranslateDocument(snap, *generated, std::vector<std::string>{lines[i]}, fresh, observer, options);
            }
        }
    }
    Dump(set, c);

    // Each golden or produced tag, line by line.
    std::vector<std::string> tags;
    for (const auto& [tag, byLine] : golden) tags.push_back(tag);
    for (const auto& [tag, byLine] : c.records) {
        if (std::find(tags.begin(), tags.end(), tag) == tags.end()) tags.push_back(tag);
    }
    std::vector<int> keys;
    for (const auto& [tag, byLine] : golden) {
        for (const auto& [k, v] : byLine) keys.push_back(k);
    }
    for (const auto& [tag, byLine] : c.records) {
        for (const auto& [k, v] : byLine) keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    const std::vector<std::string> none;
    auto list = [&](const Records& r, const std::string& tag, int k) -> const std::vector<std::string>& {
        const auto t = r.find(tag);
        if (t == r.end()) return none;
        const auto i = t->second.find(k);
        return i == t->second.end() ? none : i->second;
    };
    std::vector<int> expectedKeys;
    for (const auto& [tag, byLine] : golden) {
        for (const auto& [key, records] : byLine) expectedKeys.push_back(key);
    }
    std::sort(expectedKeys.begin(), expectedKeys.end());
    expectedKeys.erase(std::unique(expectedKeys.begin(), expectedKeys.end()), expectedKeys.end());
    HD_CHECK_EQ(keys.size(), expectedKeys.size());
    std::string inputRecords;
    HD_REQUIRE(ReadFile(GoldenDir() + "/stages/" + set + ".IN.tsv", inputRecords));
    HD_CHECK_EQ(lines.size(), hdtest::CountRecords(inputRecords, "IN"));
    for (const auto& [tag, byLine] : golden) {
        size_t expected = 0, compared = 0;
        for (const auto& [key, records] : byLine) expected += records.size();
        for (int key : keys) compared += list(golden, tag, key).size();
        HD_CHECK_EQ(compared, expected);
    }
    for (const int k : keys) {
        std::string first;
        for (const std::string& tag : tags) {
            const auto& g = list(c.records, tag, k);
            const auto& w = list(golden, tag, k);
            totals.records[tag] += static_cast<long long>(w.size());
            if (Differs(tag, g, w, totals.sColumns)) totals.differing[tag]++;
        }
        for (const std::string& tag : kLineStages) {
            if (raw && tag != "S" && tag != "K") continue;
            const auto& g = list(c.records, tag, k);
            const auto& w = list(golden, tag, k);
            if (Differs(tag, g, w, totals.sColumns) && first.empty()) first = tag;
        }
        totals.lines++;
        if (first.empty()) {
            totals.identical++;
        } else {
            totals.firstStage[first]++;
        }
    }
    std::printf("driver_golden %s: %lld lines, identical so far %lld\n", set.c_str(), totals.lines, totals.identical);
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
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/stages/"));
    const std::string golden = GoldenDir();
    std::error_code ec;
    if (!std::filesystem::exists(golden + "/stages/MANIFEST", ec) || Dictionaries() == nullptr) {
        HD_SKIP(std::string("no ") + golden + "/stages/MANIFEST or dictionaries");
    }
    const auto sets = hdtest::ManifestSets(golden + "/stages/", ".S.tsv");
    HD_REQUIRE(!sets.empty());
    bool rawRan = false, eachRan = false, docRan = false, extraRan = false;

    Totals totals;
    for (const std::string& set : sets) {
        if (set != "corpus.raw" && set != "corpus2.each" && !set.starts_with("doc-") && !set.starts_with("x-")) continue;
        std::string text;
        HD_REQUIRE(ReadFile(golden + "/" + set + ".in", text));
        rawRan = rawRan || set == "corpus.raw";
        eachRan = eachRan || set == "corpus2.each";
        docRan = docRan || set.starts_with("doc-");
        extraRan = extraRan || set.starts_with("x-");
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
    for (const auto& [tag, n] : totals.differing) gated += n;
    std::printf("driver_golden: gated records differing %lld; %lld lines, %lld identical\n", gated, totals.lines,
                totals.identical);
    for (const auto& [stage, n] : totals.firstStage) std::printf("  first differing stage %-4s %lld lines\n", stage.c_str(), n);
    for (const auto& [col, n] : totals.sColumns) std::printf("  S column %zu differs in %lld records\n", col, n);
    HD_CHECK(rawRan && eachRan && docRan && extraRan);
    for (const char* tag : {"T", "L", "N", "P", "S", "K", "C", "R1", "R2", "R3", "OUT"}) {
        HD_CHECK(totals.records[tag] > 0);
    }
    HD_CHECK_EQ(totals.identical, totals.lines);
    HD_CHECK_EQ(gated, 0LL);
}
