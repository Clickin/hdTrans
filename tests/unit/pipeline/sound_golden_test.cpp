// The sound stage against the stage records of the expectation sets of the text path
// ($HDTRANS_GOLDEN/stages/<set>.{N,CTX,SN}.tsv): for every case or document line with
// an N record, Sound of it must give the SN record. The dictionaries come from
// $HDTRANS_DICTS and the plain entries
// of each line from $HDTRANS_GOLDEN/plain/ (head.tsv and tail.tsv around the E rows of
// <set>.tsv); the confirmed names and person places from the CTX records. Skipped when
// a folder is absent.
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

#include "core/dict/snapshot.h"
#include "core/pipeline/sound.h"
#include "hdtest.h"
#include "golden.h"
#include "output/stage_records.h"

using namespace hdTrans;
using namespace stagerecords;

namespace {

int Int(std::string_view s) { return std::atoi(std::string(s).c_str()); }

using hdtest::DictsDir;

// An entry row of the plain folder: key target pos sem gram prio file line order flags
// negative.
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

dict::KeySet Fields(std::string_view s) {
    dict::KeySet out;
    if (s.empty()) return out;
    for (const auto f : Split(s, '\t')) out.emplace(f);
    return out;
}

// The accepted names of a document from its doc.key record (the names' dictionary, the
// accepted names and the other named surfaces, separated by NUL).
dict::KeySet AcceptedNames(std::string_view key) {
    const auto parts = Split(key, '\0');
    return parts.size() > 1 ? Fields(parts[1]) : dict::KeySet{};
}

}  // namespace

HD_TEST(pipeline_sound_golden) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/stages/"));
    const std::string dir = GoldenDir() + "/stages/";
    const std::string plainDir = GoldenDir() + "/plain/";
    std::error_code ec;
    if (!std::filesystem::exists(dir + "MANIFEST", ec) || !std::filesystem::is_directory(plainDir, ec) ||
        !std::filesystem::is_directory(DictsDir(), ec)) {
        HD_SKIP(std::string("no ") + dir + "MANIFEST, " + plainDir + " or " + DictsDir());
    }
    std::vector<std::string> sets;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        const std::string suffix = ".SN.tsv";
        if (name.size() <= suffix.size() || !name.ends_with(suffix)) continue;
        const std::string set = name.substr(0, name.size() - suffix.size());
        if (!set.ends_with(".raw")) sets.push_back(set);
    }
    std::sort(sets.begin(), sets.end());
    HD_REQUIRE(!ec);
    HD_REQUIRE(!sets.empty());

    dict::LoadResult loaded = dict::LoadFolders({DictsDir()});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const dict::Snapshot& snap = *loaded.snapshot;
    dict::CodeTable codes;
    std::vector<dict::UserEntry> head, tail;
    HD_REQUIRE(ReadEntries(plainDir + "head.tsv", codes, head));
    HD_REQUIRE(ReadEntries(plainDir + "tail.tsv", codes, tail));
    std::map<std::string, std::unique_ptr<dict::UserText>> texts;

    size_t allLines = 0, allChanged = 0, allDiffering = 0, shown = 0;
    for (const auto& set : sets) {
        std::string nFile, ctxFile, snFile, plainFile;
        HD_REQUIRE(ReadFile(dir + set + ".N.tsv", nFile));
        HD_REQUIRE(ReadFile(dir + set + ".CTX.tsv", ctxFile));
        HD_REQUIRE(ReadFile(dir + set + ".SN.tsv", snFile));
        HD_REQUIRE(ReadFile(plainDir + set + ".tsv", plainFile));
        const auto n = Blocks(nFile), ctx = Blocks(ctxFile), sn = Blocks(snFile), plain = Blocks(plainFile);
        HD_REQUIRE(n.size() == sn.size() && n.size() == ctx.size() && n.size() == plain.size());
        size_t lines = 0, changed = 0, differing = 0;
        dict::KeySet accepted;
        for (size_t b = 0; b < n.size(); b++) {
            HD_REQUIRE(n[b].header == sn[b].header && n[b].header == ctx[b].header && n[b].header == plain[b].header);
            dict::KeySet skip, persons;
            for (const auto r : ctx[b].records) {
                const auto f = Split(r, '\t');
                if (f.size() != 3) continue;
                if (f[1] == "doc.key") accepted = AcceptedNames(UnescapeStageText(f[2]));
                if (f[1] == "line.skip") skip.insert(UnescapeStageText(f[2]));
                if (f[1] == "line.persons") persons.insert(UnescapeStageText(f[2]));
            }
            if (n[b].records.empty()) continue;  // a whole line from the line table
            HD_REQUIRE(sn[b].records.size() == 1 && n[b].records.size() == 1);
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
            for (const auto& name : accepted) {
                if (!skip.contains(name)) confirmed.insert(name);
            }
            pipeline::LineContext lc;
            lc.lookup.plain = text.get();
            lc.lookup.confirmed = &confirmed;
            lc.lookup.persons = &persons;
            const std::string_view nRecord = n[b].records[0], want = sn[b].records[0];
            HD_REQUIRE(nRecord.starts_with("N\t") && want.starts_with("SN\t"));
            const std::string normalized = UnescapeStageText(nRecord.substr(2));
            const std::string got = "SN\t" + EscapeStageText(pipeline::Sound(normalized, lc, snap));
            lines++;
            if (nRecord.substr(2) != want.substr(3)) changed++;
            if (got == want) continue;
            differing++;
            if (shown++ < 20) {
                std::fprintf(stderr, "%s %.*s\n  in   %.*s\n  want %.*s\n  got  %s\n", set.c_str(),
                             static_cast<int>(n[b].header.size()), n[b].header.data(), static_cast<int>(nRecord.size()),
                             nRecord.data(), static_cast<int>(want.size()), want.data(), got.c_str());
            }
        }
        HD_CHECK_EQ(lines, hdtest::CountRecords(snFile, "SN"));
        allLines += lines;
        allChanged += changed;
        allDiffering += differing;
        if (differing > 0) std::printf("pipeline sound golden %s: %zu lines, %zu differing\n", set.c_str(), lines, differing);
    }
    std::printf("pipeline sound golden: %zu sets, %zu SN records, %zu changed by the stage, %zu differing\n", sets.size(),
                allLines, allChanged, allDiffering);
    HD_CHECK(allLines > 0);
    HD_CHECK_EQ(allDiffering, size_t{0});
}
