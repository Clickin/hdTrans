// hdrulec: the rule compiler.
//   hdrulec lint RULES [-dicts DIR] [-tsv] [-max-delta N]
//       parses the flat rules (or readable rules, *.hdr) and runs the checks; exit 1 on errors
//   hdrulec convert RULES [-o OUT]       the flat rules in the readable syntax
//   hdrulec flat RULES.hdr [-o OUT]      readable rules in the flat format
//   hdrulec verify RULES [-paths N]      checks the front end against rules::Load and the
//                                        conversion round trip on every rule (at most N
//                                        evaluation paths per rule, default 2^20)
//   hdrulec cpp RULES -local FIRST-LAST [-globals] [-o OUT | -check FILE]
//   hdrulec tests RULES -o OUT           the test lines as unit tests
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/rules/rules.h"
#include "rulec/oracle.h"
#include "rulec/rulec.h"

using namespace hdTrans;
using namespace hdTrans::rulec;

namespace {

bool ReadFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[1 << 16];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool WriteFile(const std::string& path, const std::string& text) {
    if (path.empty()) return std::fwrite(text.data(), 1, text.size(), stdout) == text.size();
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

bool Readable(const std::string& path) { return path.size() > 4 && path.substr(path.size() - 4) == ".hdr"; }

int Report(const Source& source, const Diagnostics& diags, bool tsv) {
    if (tsv) std::fputs("kind\tindex\tstatement\tline\tcolumn\tseverity\tflag\tmessage\n", stdout);
    for (const Diagnostic& d : diags.list) {
        const std::string text = tsv ? FormatRow(source, d) : Format(source, d);
        std::fputs(text.c_str(), tsv ? stdout : stderr);
    }
    if (!tsv && (diags.errors || diags.warnings))
        std::fprintf(stderr, "%d error(s), %d warning(s)\n", diags.errors, diags.warnings);
    return diags.errors ? 1 : 0;
}

bool SameProgram(const rules::Program& a, const rules::Program& b) {
    if (a.nodes.size() != b.nodes.size() || a.literals != b.literals || a.localRules != b.localRules ||
        a.globalRules.size() != b.globalRules.size())
        return false;
    for (std::size_t i = 0; i < a.nodes.size(); i++) {
        const rules::Node &x = a.nodes[i], &y = b.nodes[i];
        if (x.kind != y.kind || x.op != y.op || x.delta != y.delta || x.rightDelta != y.rightDelta || x.first != y.first ||
            x.second != y.second)
            return false;
    }
    for (std::size_t i = 0; i < a.globalRules.size(); i++) {
        const rules::GlobalRule &x = a.globalRules[i], &y = b.globalRules[i];
        if (x.pumsa != y.pumsa || x.number != y.number || x.uganRule != y.uganRule || x.root != y.root) return false;
    }
    return true;
}

// The front end against rules::Load, and the readable round trip on every rule: same
// normalized trees, and the same calls for every answer script (Oracle).
int Verify(const Source& source, const File& file, long long cap) {
    int failures = 0;
    rules::Program loaded;
    rules::LoadError error{};
    if (!rules::Load(source.text, loaded, error)) {
        std::fprintf(stderr, "rules::Load rejects the file at line %d: %s\n", error.line, error.message);
        return 1;
    }
    const rules::Program lowered = Lower(file);
    std::printf("front end vs rules::Load: %s (%zu nodes, %zu literal bytes)\n",
                SameProgram(loaded, lowered) ? "identical programs" : "DIFFERENT programs", loaded.nodes.size(),
                loaded.literals.size());
    failures += !SameProgram(loaded, lowered);

    const std::string readable = PrintReadable(file);
    Source back(source.name + ".hdr", readable);
    Diagnostics diags;
    File again = ParseReadable(back, diags);
    if (diags.errors) {
        Report(back, diags, false);
        return 1;
    }
    File a = file;
    Normalize(a);
    CanonicalLiterals(a);
    Normalize(again);
    CanonicalLiterals(again);
    int treeDiffs = 0;
    if (a.local.size() != again.local.size() || a.global.size() != again.global.size()) treeDiffs++;
    for (std::size_t i = 0; i < a.local.size() && i < again.local.size(); i++) {
        const bool same = SameTree(a.local[i].body, again.local[i].body) && a.local[i].tests.size() == again.local[i].tests.size();
        if (!same) std::fprintf(stderr, "L %zu differs after the round trip\n", i);
        treeDiffs += !same;
    }
    for (std::size_t i = 0; i < a.global.size() && i < again.global.size(); i++) {
        const Rule &x = a.global[i], &y = again.global[i];
        const bool same = SameTree(x.body, y.body) && x.pumsa == y.pumsa && x.number == y.number && x.ugan == y.ugan;
        if (!same) std::fprintf(stderr, "G %zu differs after the round trip\n", i);
        treeDiffs += !same;
    }
    std::printf("readable round trip: %zu local and %zu global rules, %d tree differences, %zu bytes of readable text\n",
                again.local.size(), again.global.size(), treeDiffs, readable.size());
    failures += treeDiffs;

    // The flat text printed from the readable tree loads and evaluates like the original.
    rules::Program converted;
    const std::string flat = PrintFlat(again);
    if (!rules::Load(flat, converted, error)) {
        std::fprintf(stderr, "the converted flat text does not load: line %d: %s\n", error.line, error.message);
        return 1;
    }
    // A rule may differ from the original only in the spelling of surface markers ("22x" is
    // printed as ends "x" and read back as "Rx"); it must then match the original with
    // canonical literals.
    File canonicalFile = file;
    CanonicalLiterals(canonicalFile);
    const rules::Program canonical = Lower(canonicalFile);
    Oracle oracle;
    long long paths = 0, capped = 0, differ = 0, spelling = 0;
    auto run = [&](const rules::Program& p, uint32_t root) {
        return oracle.Paths([&](rules::Item* it) { return rules::RunRule(p, root, it); }, cap);
    };
    auto compare = [&](uint32_t rootA, uint32_t rootB, const char* kind, std::size_t index) {
        const auto x = run(loaded, rootA);
        const auto y = run(converted, rootB);
        paths += x.paths;
        capped += x.capped;
        if (x.paths == y.paths && x.hash == y.hash) return;
        const auto z = run(canonical, kind[0] == 'L' ? canonical.localRules[index] : canonical.globalRules[index].root);
        if (z.paths == y.paths && z.hash == y.hash) {
            spelling++;
            return;
        }
        std::fprintf(stderr, "%s %zu evaluates differently after the round trip\n", kind, index);
        differ++;
    };
    for (std::size_t i = 0; i < loaded.localRules.size(); i++) compare(loaded.localRules[i], converted.localRules[i], "L", i);
    for (std::size_t i = 0; i < loaded.globalRules.size(); i++)
        compare(loaded.globalRules[i].root, converted.globalRules[i].root, "G", i);
    std::printf("call-trace equivalence of the round trip: %lld paths, %lld rules stopped at %lld paths, "
                "%lld rules differ only in marker spelling, %lld rules differ\n",
                paths, capped, cap, spelling, differ);
    failures += static_cast<int>(differ);
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: hdrulec lint|convert|flat|verify|cpp|tests RULES [options]\n");
        return 2;
    }
    const std::string command = argv[1], path = argv[2];
    std::string out, dicts, check;
    bool tsv = false, globals = false;
    int first = 0, last = -1;
    long long cap = 1 << 20;
    CheckOptions options;
    for (int i = 3; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a == "-dicts" && i + 1 < argc) dicts = argv[++i];
        else if (a == "-check" && i + 1 < argc) check = argv[++i];
        else if (a == "-tsv") tsv = true;
        else if (a == "-globals") globals = true;
        else if (a == "-paths" && i + 1 < argc) cap = std::atoll(argv[++i]);
        else if (a == "-max-delta" && i + 1 < argc) options.maxDelta = std::atoi(argv[++i]);
        else if (a == "-local" && i + 1 < argc && std::sscanf(argv[++i], "%d-%d", &first, &last) == 2) {}
        else {
            std::fprintf(stderr, "hdrulec: unknown option %s\n", a.c_str());
            return 2;
        }
    }
    std::string text;
    if (!ReadFile(path, text)) {
        std::fprintf(stderr, "hdrulec: cannot read %s\n", path.c_str());
        return 2;
    }
    const Source source(path, std::move(text));
    Diagnostics diags;
    File file = Readable(path) ? ParseReadable(source, diags) : ParseFlat(source, diags);
    if (diags.errors) return Report(source, diags, tsv);

    if (command == "lint") {
        Inventory inventory;
        std::string error;
        if (!dicts.empty() && !LoadInventory(dicts, inventory, error)) {
            std::fprintf(stderr, "hdrulec: %s\n", error.c_str());
            return 2;
        }
        File normalized = file;
        Normalize(normalized);
        Check(normalized, inventory, options, diags);
        return Report(source, diags, tsv);
    }
    if (command == "convert") return WriteFile(out, PrintReadable(file)) ? 0 : 2;
    if (command == "flat") return WriteFile(out, PrintFlat(file)) ? 0 : 2;
    if (command == "verify") return Verify(source, file, cap);
    if (command == "tests") return WriteFile(out, EmitTests(file, source)) ? 0 : 2;
    if (command == "cpp") {
        if (Readable(path)) {
            std::fprintf(stderr, "hdrulec: cpp needs the flat rules, whose record lines it hashes\n");
            return 2;
        }
        const std::string code = EmitCpp(file, source, first, last, globals);
        if (check.empty()) return WriteFile(out, code) ? 0 : 2;
        std::string current;
        if (!ReadFile(check, current) || current != code) {
            std::fprintf(stderr, "hdrulec: %s is not the code of %s; regenerate it with hdrulec cpp\n", check.c_str(),
                         path.c_str());
            return 1;
        }
        return 0;
    }
    std::fprintf(stderr, "hdrulec: unknown command %s\n", command.c_str());
    return 2;
}
