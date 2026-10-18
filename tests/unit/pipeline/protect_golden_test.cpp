// The protect and segment stages against the stage records of the expectation sets of
// the text path ($HDTRANS_GOLDEN/stages/<set>.{SN,CTX,P}.tsv):
//   - for every case or document line with an SN record, Protect of it must give the P
//     records;
//   - Segment of the P pieces must give the CH records.
// Protect reads the dictionaries ($HDTRANS_DICTS, by default dicts/base next to the
// repository) and the plain entries of each line, which a session builds from its
// dictionaries and the name registry of the document; here they come compiled in
// $HDTRANS_GOLDEN/plain/: head.tsv and tail.tsv around the registry and historical
// entries of each line (<set>.tsv, E rows under the case and line headers of the stage
// files), one entry per row (key target pos sem gram prio file line order flags
// negative). The confirmed names, the person places and the names of the document come
// from the CTX records. Skipped when a folder is absent.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/pipeline/protect.h"
#include "hdTrans/pipeline/segment.h"
#include "hdTrans/text/utf8.h"
#include "hdtest.h"
#include "output/stage_records.h"

using namespace hdTrans;
using namespace stagerecords;

namespace {

int Int(std::string_view s) { return std::atoi(std::string(s).c_str()); }

std::string DictsDir() {
    if (const char* d = std::getenv("HDTRANS_DICTS"); d != nullptr && *d != 0) return d;
    std::string f = __FILE__;
    const size_t slash = f.find_last_of("/\\");
    return (slash == std::string::npos ? std::string(".") : f.substr(0, slash)) + "/../../../../dicts/base";
}

// Entry rows as written in the plain folder.
bool ParseEntry(std::string_view row, dict::CodeTable& codes, dict::UserEntry& e) {
    const auto f = Split(row, '\t');
    if (f.size() != 11) return false;
    e.key = std::string(f[0]);
    e.target = std::string(f[1]);
    e.codes = codes.At(codes.Intern(f[2], f[3], f[4], "GN"));
    e.prio = Int(f[5]);
    e.file = Int(f[6]);
    e.line = Int(f[7]);
    e.order = Int(f[8]);
    std::string reg;
    for (const char c : f[9]) {
        switch (c) {
            case 'N': e.name = true; break;
            case 'L': e.nameLayer = true; break;
            case 'K': e.kana = true; break;
            case 'F': e.force = true; break;
            case 'H': e.hidden = true; break;
            case 'O': e.honorific = true; break;
            case 'E': e.end = true; break;
            case 'P': e.polite = true; break;
            case 'S': e.stative = true; break;
            case 'v': case 'c': case 'l': e.finalClass = c; break;
            default: reg += c;
        }
    }
    if (!reg.empty()) e.registerLevel = static_cast<uint8_t>(std::atoi(reg.c_str()));
    e.negative = std::string(f[10]);
    return true;
}

bool ReadEntries(const std::string& path, dict::CodeTable& codes, std::vector<dict::UserEntry>& out) {
    std::string text;
    if (!ReadFile(path, text)) return false;
    for (const auto row : Split(text, '\n')) {
        if (row.empty()) continue;
        dict::UserEntry e;
        if (!ParseEntry(row, codes, e)) return false;
        out.push_back(std::move(e));
    }
    return true;
}

std::vector<std::string> PieceRecords(std::string_view tag, const output::Pieces& p) {
    std::vector<std::string> out;
    for (size_t i = 0; i < p.size(); i++) {
        out.push_back(std::string(tag) + "\t" + std::to_string(i) + "\t" + std::to_string(static_cast<int>(p[i].span)) +
                      "\t" + EscapeStageText(p[i].text));
    }
    return out;
}

std::vector<std::string> ChunkRecords(const std::vector<pipeline::Chunk>& chunks) {
    std::vector<std::string> out;
    for (size_t c = 0; c < chunks.size(); c++) {
        for (const std::string& r : PieceRecords("CH", chunks[c])) {
            out.push_back("CH\t" + std::to_string(c) + r.substr(2));
        }
    }
    return out;
}

dict::KeySet Fields(std::string_view s) {
    dict::KeySet out;
    if (s.empty()) return out;
    for (const auto f : Split(s, '\t')) out.emplace(f);
    return out;
}

// The registry state of a document: its accepted names and every surface it shows to
// be a name, from the doc.key record (the names' dictionary, the accepted names and the
// other named surfaces, separated by NUL).
struct DocNames {
    dict::KeySet accepted;
    dict::KeySet named;
};

DocNames ParseDocKey(std::string_view key) {
    DocNames d;
    const auto parts = Split(key, '\0');
    if (parts.size() > 1) d.accepted = Fields(parts[1]);
    d.named = d.accepted;
    if (parts.size() > 2) {
        for (const auto& t : Fields(parts[2])) d.named.insert(t);
    }
    return d;
}

struct Totals {
    size_t lines = 0, pRecords = 0, pDiffering = 0, chRecords = 0, chDiffering = 0;
};

// Compares got with want; prints the first differences of a set.
size_t Diff(const std::vector<std::string>& got, const std::vector<std::string_view>& want, std::string_view where,
            size_t& shown) {
    size_t n = 0;
    for (size_t i = 0; i < std::max(got.size(), want.size()); i++) {
        const std::string_view g = i < got.size() ? std::string_view(got[i]) : "<none>";
        const std::string_view w = i < want.size() ? want[i] : "<none>";
        if (g == w) continue;
        n++;
        if (shown++ < 10) {
            std::fprintf(stderr, "%.*s\n  want %.*s\n  got  %.*s\n", static_cast<int>(where.size()), where.data(),
                         static_cast<int>(w.size()), w.data(), static_cast<int>(g.size()), g.data());
        }
    }
    return n;
}

}  // namespace

HD_TEST(pipeline_protect_segment_golden) {
    const std::string dir = GoldenDir() + "/stages/";
    const std::string plainDir = GoldenDir() + "/plain/";
    std::error_code ec;
    if (!std::filesystem::exists(dir + "MANIFEST", ec)) {
        std::printf("skip pipeline_protect_segment_golden: no %sMANIFEST\n", dir.c_str());
        return;
    }
    const bool withProtect = std::filesystem::is_directory(plainDir, ec) && std::filesystem::is_directory(DictsDir(), ec);
    if (!withProtect) std::printf("pipeline_protect_segment_golden: no %s or %s, Segment only\n", plainDir.c_str(), DictsDir().c_str());

    std::vector<std::string> sets;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        const std::string suffix = ".SN.tsv";
        if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        const std::string set = name.substr(0, name.size() - suffix.size());
        if (set.ends_with(".raw")) continue;
        sets.push_back(set);
    }
    std::sort(sets.begin(), sets.end());
    HD_REQUIRE(!sets.empty());

    std::shared_ptr<const dict::Snapshot> snap;
    dict::CodeTable codes;
    std::vector<dict::UserEntry> head, tail;
    if (withProtect) {
        dict::LoadResult r = dict::LoadFolders({DictsDir()});
        HD_REQUIRE(r.snapshot != nullptr);
        snap = r.snapshot;
        HD_REQUIRE(ReadEntries(plainDir + "head.tsv", codes, head));
        HD_REQUIRE(ReadEntries(plainDir + "tail.tsv", codes, tail));
    }
    // The plain entries of a line by its registry and historical rows.
    std::map<std::string, std::unique_ptr<dict::UserText>> texts;

    Totals all;
    for (const auto& set : sets) {
        std::string snFile, ctxFile, pFile, plainFile;
        HD_REQUIRE(ReadFile(dir + set + ".SN.tsv", snFile));
        HD_REQUIRE(ReadFile(dir + set + ".CTX.tsv", ctxFile));
        HD_REQUIRE(ReadFile(dir + set + ".P.tsv", pFile));
        const auto sn = Blocks(snFile), ctx = Blocks(ctxFile), p = Blocks(pFile);
        HD_REQUIRE(sn.size() == ctx.size() && sn.size() == p.size());
        std::vector<Block> plain;
        if (withProtect) {
            HD_REQUIRE(ReadFile(plainDir + set + ".tsv", plainFile));
            plain = Blocks(plainFile);
            HD_REQUIRE(plain.size() == sn.size());
        }
        Totals t;
        size_t shown = 0;
        DocNames doc;
        for (size_t b = 0; b < sn.size(); b++) {
            HD_REQUIRE(sn[b].header == ctx[b].header && sn[b].header == p[b].header);
            dict::KeySet skip, persons;
            for (const auto r : ctx[b].records) {
                const auto f = Split(r, '\t');
                if (f.size() != 3) continue;
                if (f[1] == "doc.key") doc = ParseDocKey(UnescapeStageText(f[2]));
                if (f[1] == "line.skip") skip.insert(UnescapeStageText(f[2]));
                if (f[1] == "line.persons") persons.insert(UnescapeStageText(f[2]));
            }
            std::vector<std::string_view> wantP, wantCH;
            output::Pieces typed;
            for (const auto r : p[b].records) {
                const auto f = Split(r, '\t');
                if (f[0] == "P") {
                    wantP.push_back(r);
                    typed.push_back({UnescapeStageText(f[3]), static_cast<output::SpanKind>(Int(f[2]))});
                } else if (f[0] == "CH") {
                    wantCH.push_back(r);
                }
            }
            if (sn[b].records.empty()) continue;  // a whole line from the line table
            t.lines++;
            const std::string where = set + " " + std::string(sn[b].header);
            t.pRecords += wantP.size();
            t.chRecords += wantCH.size();
            t.chDiffering += Diff(ChunkRecords(pipeline::Segment(typed)), wantCH, where, shown);
            if (!withProtect) continue;

            HD_REQUIRE(plain[b].header == sn[b].header);
            std::string extraKey;
            for (const auto r : plain[b].records) extraKey.append(r).push_back('\n');
            auto& text = texts[extraKey];
            if (!text) {
                std::vector<dict::UserEntry> entries = head;
                for (const auto r : plain[b].records) {
                    dict::UserEntry e;
                    HD_REQUIRE(r.starts_with("E\t") && ParseEntry(r.substr(2), codes, e));
                    entries.push_back(std::move(e));
                }
                entries.insert(entries.end(), tail.begin(), tail.end());
                text = std::make_unique<dict::UserText>(std::move(entries));
            }
            dict::KeySet confirmed;
            for (const auto& n : doc.accepted) {
                if (!skip.contains(n)) confirmed.insert(n);
            }
            pipeline::LineContext lc;
            lc.lookup.plain = text.get();
            lc.lookup.confirmed = &confirmed;
            lc.lookup.persons = &persons;
            lc.named = &doc.named;
            const auto f = Split(sn[b].records[0], '\t');
            HD_REQUIRE(f.size() == 2 && f[0] == "SN");
            const std::string sounded = UnescapeStageText(f[1]);
            t.pDiffering += Diff(PieceRecords("P", pipeline::Protect(sounded, lc, *snap)), wantP, where, shown);
        }
        std::printf("pipeline golden %s: %zu lines, P %zu records %zu differing, CH %zu records %zu differing\n", set.c_str(),
                    t.lines, t.pRecords, t.pDiffering, t.chRecords, t.chDiffering);
        all.lines += t.lines;
        all.pRecords += t.pRecords;
        all.pDiffering += t.pDiffering;
        all.chRecords += t.chRecords;
        all.chDiffering += t.chDiffering;
    }
    std::printf("pipeline golden: %zu sets, %zu lines, P %zu records %zu differing%s, CH %zu records %zu differing\n",
                sets.size(), all.lines, all.pRecords, all.pDiffering, withProtect ? "" : " (not run)", all.chRecords,
                all.chDiffering);
    HD_CHECK_EQ(all.pDiffering, size_t{0});
    HD_CHECK_EQ(all.chDiffering, size_t{0});
}

// The span kinds of the protected-span cut of あcあ for every code point c but the
// surrogates ($HDTRANS_GOLDEN/stages/sweep.cp.tsv: U+XXXX, two token columns, the kinds).
HD_TEST(pipeline_cut_spans_sweep) {
    std::string file;
    if (!ReadFile(GoldenDir() + "/stages/sweep.cp.tsv", file)) {
        std::printf("skip pipeline_cut_spans_sweep: no sweep.cp.tsv\n");
        return;
    }
    size_t lines = 0, differing = 0;
    for (const auto line : Split(file, '\n')) {
        if (line.empty()) continue;
        const auto f = Split(line, '\t');
        HD_REQUIRE(f.size() == 4 && f[0].starts_with("U+"));
        const auto c = static_cast<char32_t>(std::strtoul(std::string(f[0].substr(2)).c_str(), nullptr, 16));
        std::string s = "あ";
        text::AppendRune(s, c);
        s += "あ";
        std::string kinds;
        for (const output::Piece& p : pipeline::internal::CutSpans(s, {})) {
            if (!kinds.empty()) kinds += ' ';
            kinds += std::to_string(static_cast<int>(p.span));
        }
        lines++;
        if (kinds != f[3]) {
            if (differing++ < 10) std::fprintf(stderr, "sweep %.*s: want %.*s got %s\n", static_cast<int>(f[0].size()), f[0].data(),
                                                static_cast<int>(f[3].size()), f[3].data(), kinds.c_str());
        }
    }
    std::printf("pipeline cut spans sweep: %zu code points, %zu differing\n", lines, differing);
    HD_CHECK_EQ(differing, size_t{0});
}
