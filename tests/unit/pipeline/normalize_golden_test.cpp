// Normalize against the stage records in $HDTRANS_GOLDEN/stages:
// for every line of the each and doc sets listed in MANIFEST,
// the N record of <set>.N.tsv must equal the escaped Normalize of the IN record, with the
// doc.female value of the set's CTX records; FemaleSpeech of the lines of each case must
// give that value. A line with a line.wholeline CTX record has no N record. Skipped until
// MANIFEST exists.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/pipeline/normalize.h"
#include "hdTrans/pipeline/stage_text.h"
#include "hdtest.h"
#include "golden.h"

namespace {

using hdTrans::pipeline::EscapeStageText;
using hdTrans::pipeline::LineContext;
using hdTrans::pipeline::Normalize;

using hdtest::GoldenDir;

bool ReadFile(const std::string& path, std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    return true;
}

std::vector<std::string_view> Lines(std::string_view text) {
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        out.push_back(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
    }
    return out;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::string Unescape(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out.push_back(s[i]);
            continue;
        }
        const char c = s[++i];
        if (c == 't') out.push_back('\t');
        else if (c == 'n') out.push_back('\n');
        else if (c == 'r') out.push_back('\r');
        else if (c == 'x' && i + 2 < s.size() && HexDigit(s[i + 1]) >= 0 && HexDigit(s[i + 2]) >= 0) {
            out.push_back(static_cast<char>(HexDigit(s[i + 1]) * 16 + HexDigit(s[i + 2])));
            i += 2;
        } else out.push_back(c);
    }
    return out;
}

// The records of a stage file by unit: the #CASE header, plus "\n" and the #LINE header
// inside a document.
using Units = std::map<std::string, std::vector<std::string_view>>;

Units ReadUnits(std::string_view text) {
    Units units;
    std::string caseHeader, key;
    for (std::string_view line : Lines(text)) {
        if (line.starts_with("#CASE\t")) {
            caseHeader = std::string(line);
            key = caseHeader;
        } else if (line.starts_with("#LINE\t")) {
            key = caseHeader + "\n" + std::string(line);
        } else {
            units[key].push_back(line);
        }
    }
    return units;
}

struct Totals {
    int records = 0;
    int differ = 0;
    int cases = 0;
    int femaleDiffer = 0;
};

void CheckSet(const std::string& dir, const std::string& set, Totals& t) {
    std::string inText, ctxText, nText;
    if (!ReadFile(dir + set + ".IN.tsv", inText) || !ReadFile(dir + set + ".CTX.tsv", ctxText) ||
        !ReadFile(dir + set + ".N.tsv", nText)) {
        hdtest::Fail(__FILE__, __LINE__, "missing stage files of " + set);
        return;
    }
    const auto recordsBefore = t.records;
    const Units ctx = ReadUnits(ctxText);
    const Units expected = ReadUnits(nText);
    auto ctxHas = [&](const std::string& key, std::string_view record) {
        auto it = ctx.find(key);
        if (it == ctx.end()) return false;
        for (std::string_view r : it->second) {
            if (r == record) return true;
        }
        return false;
    };

    std::string caseHeader, key;
    std::vector<std::string> caseLines;
    bool female = false;
    auto endCase = [&] {
        if (caseHeader.empty()) return;
        t.cases++;
        if (hdTrans::pipeline::internal::FemaleSpeech(caseLines) != female) {
            if (t.femaleDiffer++ < 10) hdtest::Fail(__FILE__, __LINE__, set + " doc.female: " + caseHeader);
        }
        caseLines.clear();
    };
    for (std::string_view line : Lines(inText)) {
        if (line.starts_with("#CASE\t")) {
            endCase();
            caseHeader = std::string(line);
            key = caseHeader;
            female = ctxHas(caseHeader, "CTX\tdoc.female\t1");
            continue;
        }
        if (line.starts_with("#LINE\t")) {
            key = caseHeader + "\n" + std::string(line);
            continue;
        }
        if (!line.starts_with("IN\t")) continue;
        std::string in = Unescape(line.substr(3));
        caseLines.push_back(in);
        if (in.ends_with('\r')) in.pop_back();

        std::vector<std::string_view> want;
        if (auto it = expected.find(key); it != expected.end()) want = it->second;
        bool whole = false;
        if (auto it = ctx.find(key); it != ctx.end()) {
            for (std::string_view r : it->second) whole = whole || r.starts_with("CTX\tline.wholeline\t");
        }
        std::vector<std::string> got;
        if (!whole) got.push_back("N\t" + EscapeStageText(Normalize(in, LineContext{.document = true, .female = female})));
        t.records += static_cast<int>(want.size());
        bool same = want.size() == got.size();
        for (size_t i = 0; same && i < want.size(); i++) same = want[i] == got[i];
        if (!same) {
            t.differ += static_cast<int>(want.size() > got.size() ? want.size() : got.size());
            if (t.differ <= 10) {
                std::string msg = set + " " + key + "\n  IN   " + std::string(line.substr(3));
                for (std::string_view w : want) msg += "\n  want " + std::string(w);
                for (const std::string& g : got) msg += "\n  got  " + g;
                hdtest::Fail(__FILE__, __LINE__, msg);
            }
        }
    }
    endCase();
    HD_CHECK_EQ(static_cast<size_t>(t.records - recordsBefore), hdtest::CountRecords(nText, "N"));
}

}  // namespace

HD_TEST(pipeline_normalize_golden) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    HD_REQUIRE(hdtest::CheckManifest(hdtest::GoldenDir() + "/stages/"));
    const std::string dir = GoldenDir() + "/stages/";
    std::string manifest;
    if (!ReadFile(dir + "MANIFEST", manifest)) {
        HD_SKIP(std::string("no ") + dir + "MANIFEST");
    }
    Totals t;
    int sets = 0;
    for (std::string_view line : Lines(manifest)) {
        const size_t space = line.find(' ');
        if (space == std::string_view::npos) continue;
        const std::string_view file = line.substr(space + 1);
        if (!file.ends_with(".N.tsv")) continue;
        const std::string set(file.substr(0, file.size() - 6));
        if (!set.ends_with(".each") && !set.starts_with("doc-")) continue;
        CheckSet(dir, set, t);
        sets++;
    }
    std::printf("normalize golden: %d sets, %d cases (%d doc.female differ), %d N records, %d differ\n", sets, t.cases,
                t.femaleDiffer, t.records, t.differ);
    HD_CHECK(t.records > 0);
    HD_CHECK_EQ(t.differ, 0);
    HD_CHECK_EQ(t.femaleDiffer, 0);
}
