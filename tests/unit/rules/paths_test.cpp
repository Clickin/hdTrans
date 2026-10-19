// Path hashes of all rules in $HDTRANS_GOLDEN/rules/rules.flat against
// rules/path_hashes.tsv there,
// which lists "ID\tPATHS\tHASH" per rule: Ln for local rule n, Gk for global rule k,
// the hash as 16 hex digits. Skipped when the files are absent.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "core/rules/rules.h"
#include "hdtest.h"
#include "golden.h"
#include "scripted_item.h"

namespace {

using hdtest::GoldenDir;

bool ReadFile(const std::string& path, std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    return true;
}

}  // namespace

HD_TEST(rules_path_hashes) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    const std::string dir = GoldenDir() + "/rules/";
    std::string flat, hashes;
    if (!ReadFile(dir + "rules.flat", flat) || !ReadFile(dir + "path_hashes.tsv", hashes)) {
        HD_SKIP(std::string("no ") + dir + "rules.flat and path_hashes.tsv");
    }
    hdTrans::rules::Program p;
    hdTrans::rules::LoadError e{};
    if (!hdTrans::rules::Load(flat, p, e)) {
        hdtest::Fail(__FILE__, __LINE__, "rules.flat line " + std::to_string(e.line) + ": " + e.message);
        return;
    }
    rulestest::Scripted s;
    long long rules = 0, paths = 0, mismatches = 0;
    std::string_view rest = hashes;
    while (!rest.empty()) {
        const size_t end = rest.find('\n');
        const std::string line(rest.substr(0, end));
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
        char kind = 0;
        unsigned index = 0;
        long long wantPaths = 0;
        uint64_t wantHash = 0;
        if (std::sscanf(line.c_str(), "%c%u\t%lld\t%" SCNx64, &kind, &index, &wantPaths, &wantHash) != 4 ||
            (kind != 'L' && kind != 'G') ||
            index >= (kind == 'L' ? p.localRules.size() : p.globalRules.size())) {
            hdtest::Fail(__FILE__, __LINE__, "bad path_hashes.tsv line: " + line);
            return;
        }
        const uint32_t root = kind == 'L' ? p.localRules[index] : p.globalRules[index].root;
        const rulestest::PathSummary got = rulestest::Paths(p, root, s);
        rules++;
        paths += got.paths;
        if (got.paths != wantPaths || got.hash != wantHash) {
            if (++mismatches <= 20) {
                char msg[160];
                std::snprintf(msg, sizeof msg, "%c%u: %lld paths, hash %016" PRIx64 "; want %lld, %016" PRIx64, kind,
                              index, got.paths, got.hash, wantPaths, wantHash);
                hdtest::Fail(__FILE__, __LINE__, msg);
            }
        }
    }
    std::printf("rules_path_hashes: %lld rules, %lld paths, %lld mismatches\n", rules, paths, mismatches);
    HD_CHECK_EQ(rules, static_cast<long long>(p.localRules.size() + p.globalRules.size()));
    HD_CHECK(rules > 0 && paths > 0);
    HD_CHECK_EQ(static_cast<size_t>(rules), hdtest::LineCount(hashes));
    HD_CHECK_EQ(mismatches, 0LL);
}
