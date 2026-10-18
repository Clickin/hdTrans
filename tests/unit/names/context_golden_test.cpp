// The document context against the stage goldens ($HDTRANS_GOLDEN): for every document
// set (doc-*, x-*) and every case of corpus2.each, the CTX records that the context gives
// are compared with the CTX records of the set, in order; the plain entries of the
// document (the E rows of $HDTRANS_GOLDEN/plain) are compared for the sample sets.
// Skipped when the golden folder or the dictionaries are absent.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/pipeline/line_context.h"
#include "hdTrans/names/document.h"
#include "hdtest.h"
#include "golden.h"
#include "output/stage_records.h"

using namespace hdTrans;
using namespace stagerecords;

namespace {

namespace fs = std::filesystem;

using hdtest::DictsDir;

// The lines of a set as the dump splits them at each "\n": a final "\n" leaves an
// empty last line. The lines keep their "\r".
std::vector<std::string> DumpLines(std::string_view text) {
    std::vector<std::string> out;
    for (;;) {
        const size_t nl = text.find('\n');
        out.emplace_back(text.substr(0, nl));
        if (nl == std::string_view::npos) break;
        text = text.substr(nl + 1);
    }
    return out;
}

// The sorted values of a context kind as CTX records.
void Values(std::vector<std::string>& out, const char* kind, std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    for (const std::string& v : values) out.push_back(std::string("CTX\t") + kind + "\t" + EscapeStageText(v));
}

// The CTX records of the document context, in the order of the stage records.
std::vector<std::string> DocumentRecords(const pipeline::DocumentContext& doc) {
    std::vector<std::string> out;
    Values(out, "doc.key", {doc.registryKey});
    out.push_back(std::string("CTX\tdoc.female\t") + (doc.female ? "1" : "0"));
    Values(out, "doc.persons", doc.persons);
    Values(out, "doc.verse", doc.verse);
    std::vector<std::string> rows;
    for (const auto row : Split(doc.historical, '\n')) {
        if (!row.empty()) rows.emplace_back(row);
    }
    Values(out, "doc.historical", rows);
    return out;
}

// The CTX records of one line: names left out, person places, the whole-line row.
std::vector<std::string> LineRecords(const pipeline::LineFacts& facts) {
    std::vector<std::string> out;
    Values(out, "line.skip", facts.skip);
    Values(out, "line.persons", facts.persons);
    if (facts.wholeLine) Values(out, "line.wholeline", {*facts.wholeLine});
    return out;
}

// The E row of a plain entry, in the format of the plain golden files.
std::string EntryRow(const dict::UserEntry& e) {
    std::string flags;
    if (e.name) flags += 'N';
    if (e.nameLayer) flags += 'L';
    if (e.kana) flags += 'K';
    if (e.force) flags += 'F';
    if (e.hidden) flags += 'H';
    if (e.honorific) flags += 'O';
    if (e.end) flags += 'E';
    if (e.polite) flags += 'P';
    if (e.stative) flags += 'S';
    if (e.finalClass != 0) flags += e.finalClass;
    if (e.registerLevel != 0) flags += std::to_string(static_cast<int>(e.registerLevel));
    std::string row = e.key + "\t" + e.target + "\t";
    if (e.codes != nullptr) {
        row += std::string(e.codes->partText) + "\t" + std::string(e.codes->sem.Text()) + "\t" + std::string(e.codes->gram.Text());
    } else {
        row += "\t\t";
    }
    row += "\t" + std::to_string(e.prio) + "\t" + std::to_string(e.file) + "\t" + std::to_string(e.line) + "\t" +
           std::to_string(e.order) + "\t" + flags + "\t" + e.negative;
    return row;
}

// The lines of corpus2.each as the dump reads them (lines of the cli: a final newline ends the
// last line rather than starting an empty one).
std::vector<std::string> EachLines(std::string_view text) {
    std::vector<std::string> out;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        out.emplace_back(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
    }
    return out;
}

// The records of a set in the stage file's order: the case header, the document records,
// and for document sets a #LINE header and the line's records per line.
std::vector<std::string> ContextStream(const dict::Snapshot& snap, const std::string& name, bool each,
                                       const std::vector<std::string>& lines, size_t caseNumber) {
    std::vector<std::string> out;
    out.push_back(each ? "#CASE\t" + std::to_string(caseNumber) + "\teach" : "#CASE\t" + name + "\tdoc");
    auto doc = pipeline::BuildDocument(snap, lines);
    for (const std::string& r : DocumentRecords(*doc)) out.push_back(r);
    for (size_t i = 0; i < lines.size(); i++) {
        if (!each) out.push_back("#LINE\t" + std::to_string(i + 1));
        const pipeline::LineFacts facts = pipeline::ReadLine(*doc, lines[i]);
        for (const std::string& r : LineRecords(facts)) out.push_back(r);
    }
    return out;
}

// Compares got with want block by block (a block is a header and the records after it).
// Returns the number of differing records; a header that differs counts as a record, and so
// does a record without a counterpart.
size_t Compare(const std::vector<std::string>& got, const std::vector<std::string_view>& want, std::string_view where) {
    std::vector<std::string_view> g(got.begin(), got.end());
    struct Block {
        std::string_view header;
        std::vector<std::string_view> records;
    };
    auto blocks = [](const std::vector<std::string_view>& flat) {
        std::vector<Block> out;
        for (const std::string_view r : flat) {
            if (!r.empty() && r[0] == '#') {
                out.push_back({r, {}});
            } else if (out.empty()) {
                out.push_back({std::string_view(), {}});
                out.back().records.push_back(r);
            } else {
                out.back().records.push_back(r);
            }
        }
        return out;
    };
    const std::vector<Block> gb = blocks(g), wb = blocks(want);
    size_t n = 0, shown = 0;
    for (size_t i = 0; i < std::max(gb.size(), wb.size()); i++) {
        const std::string_view gh = i < gb.size() ? gb[i].header : "<none>";
        const std::string_view wh = i < wb.size() ? wb[i].header : "<none>";
        if (gh != wh) {
            n++;
            if (shown++ < 5) std::fprintf(stderr, "%.*s block %zu: want %.*s got %.*s\n", static_cast<int>(where.size()), where.data(), i, static_cast<int>(wh.size()), wh.data(), static_cast<int>(gh.size()), gh.data());
            continue;
        }
        const std::vector<std::string_view> empty;
        const auto& gr = i < gb.size() ? gb[i].records : empty;
        const auto& wr = i < wb.size() ? wb[i].records : empty;
        for (size_t k = 0; k < std::max(gr.size(), wr.size()); k++) {
            const std::string_view a = k < gr.size() ? gr[k] : "<none>";
            const std::string_view b = k < wr.size() ? wr[k] : "<none>";
            if (a == b) continue;
            n++;
            const bool historical = b.starts_with("CTX\tdoc.historical") || a.starts_with("CTX\tdoc.historical");
            if (!historical && shown++ < 5) {
                std::fprintf(stderr, "%.*s %.*s\n  want %.*s\n  got  %.*s\n", static_cast<int>(where.size()), where.data(),
                             static_cast<int>(wh.size()), wh.data(), static_cast<int>(b.size()), b.data(), static_cast<int>(a.size()), a.data());
            }
        }
    }
    return n;
}

// The stage file's records of a set: the case and line headers and each record, in order.
std::vector<std::string_view> FlatBlocks(std::string_view file) {
    std::vector<std::string_view> out;
    for (const Block& b : Blocks(file)) {
        out.push_back(b.header);
        for (const std::string_view r : b.records) out.push_back(r);
    }
    return out;
}

// The sets of the golden folder that have a stage file of CTX records: corpus2.each and the
// document sets.
std::vector<std::string> ContextSets(const std::string& golden) {
    std::vector<std::string> sets;
    for (const auto& entry : fs::directory_iterator(golden + "/stages")) {
        const std::string file = entry.path().filename().string();
        const std::string suffix = ".CTX.tsv";
        if (file.size() <= suffix.size() || file.substr(file.size() - suffix.size()) != suffix) continue;
        const std::string set = file.substr(0, file.size() - suffix.size());
        if (set.rfind("doc-", 0) == 0 || set.rfind("x-", 0) == 0) sets.push_back(set);
    }
    std::sort(sets.begin(), sets.end());
    return sets;
}

}  // namespace

HD_TEST(names_context_golden) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/stages/"));
    const std::string golden = GoldenDir();
    if (!fs::exists(golden + "/stages/MANIFEST")) {
        HD_SKIP(std::string("no stage goldens in ") + golden);
    }
    dict::LoadResult loaded = dict::LoadFolders({DictsDir()});
    if (loaded.snapshot == nullptr) {
        HD_SKIP(std::string("dictionaries not loaded from ") + DictsDir());
    }
    const dict::Snapshot& snap = *loaded.snapshot;
    size_t total = 0, totalRecords = 0;
    for (const std::string& set : ContextSets(golden)) {
        std::string want, in;
        HD_REQUIRE(ReadFile(golden + "/stages/" + set + ".CTX.tsv", want));
        HD_REQUIRE(ReadFile(golden + "/" + set + ".in", in));
        const std::vector<std::string> got = ContextStream(snap, set, false, DumpLines(in), 0);
        HD_CHECK_EQ(got.size(), hdtest::LineCount(want));
        const size_t diff = Compare(got, FlatBlocks(want), set);
        total += diff;
        totalRecords += got.size();
        std::printf("  context %-28s %6zu records, %zu differing\n", set.c_str(), got.size(), diff);
    }
    // corpus2.each: every line is a document of its own.
    std::string want, in;
    HD_REQUIRE(ReadFile(golden + "/stages/corpus2.each.CTX.tsv", want));
    HD_REQUIRE(ReadFile(golden + "/corpus2.each.in", in));
    {
        const std::vector<std::string> lines = EachLines(in);
        std::vector<std::string> got;
        for (size_t n = 0; n < lines.size(); n++) {
            const std::vector<std::string> one = ContextStream(snap, "corpus2.each", true, {lines[n]}, n + 1);
            got.insert(got.end(), one.begin(), one.end());
        }
        HD_CHECK_EQ(got.size(), hdtest::LineCount(want));
        const size_t diff = Compare(got, FlatBlocks(want), "corpus2.each");
        total += diff;
        totalRecords += got.size();
        std::printf("  context %-28s %6zu records, %zu differing\n", "corpus2.each", got.size(), diff);
    }
    HD_CHECK(totalRecords > 0);
    std::printf("  context total: %zu records, %zu differing\n", totalRecords, total);
    HD_CHECK(!want.empty());
    HD_CHECK_EQ(total, size_t{0});
}

namespace {

// The E rows of the plain entries of the document for each line (the protect input): the
// entries of the registry and historical names that the line keeps, after the entries of
// the plain dictionaries and the names it leaves out are dropped.
std::vector<std::string> PlainStream(const dict::Snapshot& snap, bool each, const std::vector<std::string>& lines,
                                     size_t caseNumber, const std::string& name) {
    std::vector<std::string> out;
    out.push_back(each ? "#CASE\t" + std::to_string(caseNumber) + "\teach" : "#CASE\t" + name + "\tdoc");
    dict::KeySet entryKeys;
    for (const dict::UserEntry& e : snap.TextPlain().Entries()) {
        if (e.file >= 0) entryKeys.insert(e.key);
    }
    auto doc = pipeline::BuildDocument(snap, lines);
    for (size_t i = 0; i < lines.size(); i++) {
        if (!each) out.push_back("#LINE\t" + std::to_string(i + 1));
        const pipeline::LineFacts facts = pipeline::ReadLine(*doc, lines[i]);
        dict::KeySet omit = entryKeys;
        omit.insert(facts.skip.begin(), facts.skip.end());
        for (const dict::UserEntry& e : doc->extra) {
            if (omit.count(e.key) == 0) out.push_back("E\t" + EntryRow(e));
        }
    }
    return out;
}

}  // namespace

HD_TEST(names_plain_golden) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/stages/"));
    const std::string golden = GoldenDir();
    if (!fs::exists(golden + "/stages/MANIFEST")) {
        HD_SKIP(std::string("no stage goldens in ") + golden);
    }
    dict::LoadResult loaded = dict::LoadFolders({DictsDir()});
    if (loaded.snapshot == nullptr) {
        HD_SKIP(std::string("dictionaries not loaded from ") + DictsDir());
    }
    const dict::Snapshot& snap = *loaded.snapshot;
    size_t total = 0, totalRecords = 0;
    for (const char* set : {"doc-lyrics-01", "doc-n8558fh-1", "doc-oos-n5024ms-2", "x-control", "x-names", "x-hangul"}) {
        std::string want, in;
        HD_REQUIRE(ReadFile(golden + "/plain/" + set + ".tsv", want));
        HD_REQUIRE(ReadFile(golden + "/" + set + ".in", in));
        const std::vector<std::string> got = PlainStream(snap, false, DumpLines(in), 0, set);
        HD_CHECK_EQ(got.size(), hdtest::LineCount(want));
        const size_t diff = Compare(got, FlatBlocks(want), set);
        total += diff;
        totalRecords += got.size();
        std::printf("  plain %-28s %6zu records, %zu differing\n", set, got.size(), diff);
    }
    // Every case of corpus2.each.
    std::string want, in;
    HD_REQUIRE(ReadFile(golden + "/plain/corpus2.each.tsv", want));
    HD_REQUIRE(ReadFile(golden + "/corpus2.each.in", in));
    {
        const std::vector<std::string> lines = EachLines(in);
        std::vector<std::string> got;
        for (size_t k = 0; k < lines.size(); k++) {
            const std::vector<std::string> one = PlainStream(snap, true, {lines[k]}, k + 1, "corpus2.each");
            got.insert(got.end(), one.begin(), one.end());
        }
        HD_CHECK_EQ(got.size(), hdtest::LineCount(want));
        const size_t diff = Compare(got, FlatBlocks(want), "corpus2.each");
        total += diff;
        totalRecords += got.size();
        std::printf("  plain %-28s %6zu records, %zu differing\n", "corpus2.each", got.size(), diff);
    }
    HD_CHECK(totalRecords > 0);
    std::printf("  plain total: %zu records compared\n", totalRecords);
    HD_CHECK(!want.empty());
    HD_CHECK_EQ(total, size_t{0});
}
