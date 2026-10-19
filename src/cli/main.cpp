// hdtrans: reads UTF-8 from stdin (or -in FILE) and writes the translation to stdout. The
// whole input is one session unless -each or -raw is given.
//   -dicts DIR[,DIR...]   dictionary folders in load order (default: dicts next to the
//                         executable, else dicts in the current directory); a first
//                         FILE.hdsnap gives the column dictionaries of the folders
//   -userdict FILE        user dictionary file, which must load (default: userdict.tsv
//                         next to the executable, left out with a warning when it does not
//                         load; empty for none)
//   -each                 a new session for every line
//   -raw                  translate each line with the word pipeline only
//   -reference            disable translation improvements
//   -verify-dicts         verify source hashes and every cache segment
//   -compile-dicts        compile and verify the dictionary cache, then exit
//   -compile-snapshot FILE  compile the column dictionaries of the folders to FILE
//                         (a .hdsnap for -dicts), then exit
//   -no-cache             read only the editable dictionaries
//   -why                  write word choices and shadow scores to stderr
//   -stages DIR           write stage records (also HDTRANS_DRIVER_DUMP)
//   -rule-counts FILE     write how often each selection rule ran and was true to FILE:
//                         kind (L or G), index, pumsa, number, ugan, runs, fires (TSV)
//   -select-shadow        score shortest targets without changing the translation
//   -misread              replace candidates the misread model scores as misreadings
//   -no-cg                turn the hand CG-lite rules off
//   -cg-learned           also run the learned CG-lite rules
//   -in FILE              read the input from FILE
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "core/engine.h"
#include "core/dict/folders.h"
#include "core/dict/snapshot_file.h"
#include "core/engine/driver.h"
#include "core/engine/rules.h"
#include "core/word/select.h"
#include "stage_records.h"

namespace {

namespace stagerecords = hdTrans::cli::stagerecords;

std::vector<std::string> SplitComma(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t comma = s.find(',', start);
        out.push_back(s.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) return out;
        start = comma + 1;
    }
}

// The folder of the running executable in UTF-8; empty when unknown.
std::string ExecutableDir() {
    std::filesystem::path exe;
#ifdef _WIN32
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    exe = path;
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) return {};
    exe = path.c_str();
#else
    std::error_code readError;
    exe = std::filesystem::read_symlink("/proc/self/exe", readError);
    if (readError) return {};
#endif
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::canonical(exe, ec);
    const std::u8string dir = (ec ? exe : canonical).parent_path().u8string();
    return std::string(reinterpret_cast<const char*>(dir.data()), dir.size());
}

bool ReadAll(std::FILE* f, std::string& out) {
    char buf[1 << 16];
    for (;;) {
        const size_t n = std::fread(buf, 1, sizeof buf, f);
        out.append(buf, n);
        if (n < sizeof buf) return std::ferror(f) == 0;
    }
}

void PrintEscaped(std::string_view text) {
    for (char c : text) {
        switch (c) {
        case '\t': std::fputs("\\t", stderr); break;
        case '\n': std::fputs("\\n", stderr); break;
        case '\r': std::fputs("\\r", stderr); break;
        case '\\': std::fputs("\\\\", stderr); break;
        default: std::fputc(static_cast<unsigned char>(c), stderr); break;
        }
    }
}

struct WhyContext {
    std::size_t lineBase = 0;
    std::size_t line = 1;
};

void WhyLine(void* context, int line) {
    auto& why = *static_cast<WhyContext*>(context);
    why.line = why.lineBase + static_cast<std::size_t>(line) + 1;
    std::fprintf(stderr, "WHY-LINE\t%zu\n", why.line);
}

void WhyResolved(void* context, int chunk, int sentence, const hdTrans::word::Chain& chain) {
    const auto& why = *static_cast<WhyContext*>(context);
    for (const hdTrans::word::Item* it = chain.head; it; it = it->next) {
        std::fprintf(stderr, "WHY\t%zu\t%d\t%d\t%d:%d\t", why.line, chunk, sentence, it->tokenIndex, it->start);
        PrintEscaped(it->group ? std::string_view(it->group->surface) : std::string_view{});
        std::fprintf(stderr, "\t%d\t%s\t%d\t", it->selected, hdTrans::word::ProvenanceName(it->choice.layer),
                     it->choice.id);
        PrintEscaped(it->EntryText(it->selected));
        std::fprintf(stderr, "\t%d\t%s\t%d\n", it->DirectIndex(),
                     hdTrans::word::ProvenanceName(it->directChoice.layer), it->directChoice.id);
        for (const hdTrans::word::ShadowChoice& shadow : it->shadowChoices) {
            std::fprintf(stderr, "SHADOW\t%zu\t%d\t%d\t%d:%d\t", why.line, chunk, sentence, it->tokenIndex,
                         it->start);
            PrintEscaped(shadow.scorer ? shadow.scorer : "");
            std::fprintf(stderr, "\t%d\t%d\t%d\t%d\t%d\t%d\t", shadow.from, shadow.candidate,
                         shadow.currentScore, shadow.score, shadow.eligible ? 1 : 0, shadow.winner ? 1 : 0);
            PrintEscaped(shadow.target);
            std::fputc('\n', stderr);
        }
    }
}

bool WriteRuleCounts(const std::string& path, const hdTrans::rules::Program& program,
                     const hdTrans::rules::Tally& tally) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fputs("kind\tindex\tpumsa\tnumber\tugan\truns\tfires\n", f);
    for (std::size_t i = 0; i < tally.localRuns.size(); i++) {
        std::fprintf(f, "L\t%zu\t\t\t\t%llu\t%llu\n", i, static_cast<unsigned long long>(tally.localRuns[i]),
                     static_cast<unsigned long long>(tally.localFires[i]));
    }
    for (std::size_t i = 0; i < tally.globalRuns.size(); i++) {
        const hdTrans::rules::GlobalRule& r = program.globalRules[i];
        std::fprintf(f, "G\t%zu\t%c\t%d\t%d\t%llu\t%llu\n", i, static_cast<char>(r.pumsa), r.number,
                     r.uganRule ? 1 : 0, static_cast<unsigned long long>(tally.globalRuns[i]),
                     static_cast<unsigned long long>(tally.globalFires[i]));
    }
    return std::fclose(f) == 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // The output bytes are the same on every platform: no text-mode CRLF translation.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    std::optional<std::string> dictsArg, userFileArg, snapshotOut;
    std::optional<std::string> ehndArg;
    std::string inPath;
    std::string ruleCountsPath;
    const char* dump = std::getenv("HDTRANS_DRIVER_DUMP");
    std::string stagesDir = dump ? dump : "";
    bool raw = false;
    bool each = false;
    bool why = false;
    bool selectShadow = false;
    bool misread = false;
    bool noCg = false;
    bool cgLearned = false;
    bool reference = false;
    bool verifyDicts = false;
    bool compileDicts = false;
    bool noCache = false;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!std::strcmp(a, "-dicts") && i + 1 < argc) dictsArg = argv[++i];
        else if (!std::strcmp(a, "-userdict") && i + 1 < argc) userFileArg = argv[++i];
        else if (!std::strcmp(a, "-in") && i + 1 < argc) inPath = argv[++i];
        else if (!std::strcmp(a, "-ehnd") && i + 1 < argc) ehndArg = argv[++i];
        else if (!std::strcmp(a, "-stages") && i + 1 < argc) stagesDir = argv[++i];
        else if (!std::strcmp(a, "-rule-counts") && i + 1 < argc) ruleCountsPath = argv[++i];
        else if (!std::strcmp(a, "-raw")) raw = true;
        else if (!std::strcmp(a, "-each")) each = true;
        else if (!std::strcmp(a, "-reference")) reference = true;
        else if (!std::strcmp(a, "-verify-dicts")) verifyDicts = true;
        else if (!std::strcmp(a, "-compile-dicts")) compileDicts = true;
        else if (!std::strcmp(a, "-compile-snapshot") && i + 1 < argc) snapshotOut = argv[++i];
        else if (!std::strcmp(a, "-no-cache")) noCache = true;
        else if (!std::strcmp(a, "-why")) why = true;
        else if (!std::strcmp(a, "-select-shadow")) selectShadow = true;
        else if (!std::strcmp(a, "-misread")) misread = true;
        else if (!std::strcmp(a, "-no-cg")) noCg = true;
        else if (!std::strcmp(a, "-cg-learned")) cgLearned = true;
        else {
            std::fprintf(stderr, "usage: hdtrans [-dicts DIR,...] [-userdict FILE] [-ehnd DIR] [-each | -raw] [-reference] [-verify-dicts] [-compile-dicts | -compile-snapshot FILE | -no-cache] [-why] [-select-shadow] [-misread] [-no-cg] [-cg-learned] [-stages DIR] [-rule-counts FILE] [-in FILE]\n"
                                 "HDTRANS_CACHE_DIR selects an absolute cache directory; empty, relative or off (case-insensitive) disables caching.\n"
                                 "HDTRANS_SNAPSHOT=off also disables caching. Default validation trusts source size, timestamps and identity.\n"
                                 "Use -verify-dicts or HDTRANS_VERIFY_DICTS=1 after edits that preserve those values.\n"
                                 "Published cache generations must not be overwritten or truncated in place.\n");
            return 2;
        }
    }

    if (static_cast<int>(compileDicts) + static_cast<int>(snapshotOut.has_value()) + static_cast<int>(noCache) > 1) {
        std::fprintf(stderr, "hdtrans: -compile-dicts, -compile-snapshot and -no-cache cannot be combined\n");
        return 2;
    }
    if (noCache) {
#ifdef _WIN32
        const int error = _putenv_s("HDTRANS_CACHE_DIR", "off");
#else
        const int error = setenv("HDTRANS_CACHE_DIR", "off", 1);
#endif
        if (error != 0) {
            std::fprintf(stderr, "hdtrans: cannot disable caching\n");
            return 1;
        }
    }
    if (hdTrans::dict::internal::RelativeCacheOverride()) {
        std::fprintf(stderr, "hdtrans: warning: relative HDTRANS_CACHE_DIR disables dictionary caching\n");
        if (compileDicts) return 1;
    }
    const std::string programDir = dictsArg && userFileArg ? std::string() : ExecutableDir();
    const std::string dicts = dictsArg ? *dictsArg : hdTrans::dict::DefaultFolder(programDir);
    const std::string userFile = userFileArg ? *userFileArg : hdTrans::dict::UserFile(programDir);
    const auto folders = SplitComma(dicts);
    if (snapshotOut) {
        if (!hdTrans::dict::CompileSnapshot(folders, *snapshotOut)) {
            std::fprintf(stderr, "hdtrans: cannot compile the dictionary snapshot; a column dictionary must load successfully\n");
            return 1;
        }
        std::printf("%s\n", snapshotOut->c_str());
        return 0;
    }
    if (compileDicts) {
        const auto path = hdTrans::dict::internal::SnapshotPath(folders);
        if (path.empty()) {
            std::fprintf(stderr, "hdtrans: dictionary caching is disabled or no cache directory is available\n");
            return 1;
        }
        const auto previous = hdTrans::dict::internal::SnapshotResourcePath(path);
        if (!hdTrans::dict::internal::CompileCache(folders)) {
            std::fprintf(stderr, "hdtrans: cannot compile dictionary cache; a column dictionary must load successfully\n");
            return 1;
        }
        const auto resource = hdTrans::dict::internal::SnapshotResourcePath(path);
        if (!previous.empty() && previous == resource) {
            std::printf("up to date\n");
            return 0;
        }
        const auto checked = hdTrans::dict::LoadFolders(folders, {}, true, false);
        if (!checked.snapshot || !checked.snapshot->Mapped() || !checked.snapshot->Healthy()) {
            std::fprintf(stderr, "hdtrans: cannot reload the compiled dictionary cache\n");
            return 1;
        }
        std::printf("%s\n", resource.c_str());
        return 0;
    }
    if (why) std::setvbuf(stderr, nullptr, _IOFBF, 1 << 16);

    hdTrans::Engine engine(reference);
    if (!engine.Open(folders, verifyDicts, userFile)) {
        std::fprintf(stderr, "hdtrans: no dictionary loaded from %s\n", dicts.c_str());
        return 1;
    }
    if (const std::string error = engine.UserFileError(); !error.empty()) {
        // A user dictionary named with -userdict must load; the default one is optional.
        std::fprintf(stderr, "hdtrans: %suser dictionary not loaded:\n%s\n", userFileArg ? "" : "warning: ",
                     error.c_str());
        if (userFileArg) return 1;
    }
    // Without -ehnd, an Ehnd folder next to the program is used when there is one.
    const std::string ehnd = ehndArg ? *ehndArg : hdTrans::dict::EhndFolder(ExecutableDir());
    if (!ehnd.empty() && !engine.OpenEhnd(ehnd)) {
        std::fprintf(stderr, "hdtrans: cannot read the Ehnd folder %s\n", ehnd.c_str());
        return 1;
    }
    for (const std::string& e : engine.FilterErrors()) std::fprintf(stderr, "hdtrans: %s\n", e.c_str());

    std::string input;
    if (inPath.empty()) {
        if (!ReadAll(stdin, input)) return 1;
    } else {
        std::FILE* f = std::fopen(inPath.c_str(), "rb");
        if (!f || !ReadAll(f, input)) {
            std::fprintf(stderr, "hdtrans: cannot read %s\n", inPath.c_str());
            return 1;
        }
        std::fclose(f);
    }

    const auto snapshot = engine.Dictionaries();
    const auto generated = hdTrans::internal::NewGenerated(*snapshot);
    const hdTrans::word::Scorer diagnosticScorers[] = {{"shortest-target", hdTrans::word::ShortestTargetScore}};
    const auto filters = engine.Filters();
    hdTrans::internal::TranslationOptions options;
    options.reference = reference;
    options.filters = filters.get();
    options.wordImprovements.misreadFilter = misread;
    if (noCg) options.wordImprovements.cgRules = false;
    options.wordImprovements.cgLearned = cgLearned;
    if (selectShadow) {
        options.shadowScorers = diagnosticScorers;
        options.selectShadow = true;
    }
    WhyContext whyContext;
    hdTrans::internal::StageObserver observer;
    if (why) {
        observer.context = &whyContext;
        observer.line = WhyLine;
        observer.resolved = WhyResolved;
    }
    stagerecords::Collector collector;
    const bool stages = !stagesDir.empty();
    collector.families = &generated->families;
    if (stages) observer = stagerecords::ObserverFor(collector, observer);
    const hdTrans::rules::Program& program = hdTrans::internal::RulesProgram();
    hdTrans::rules::Tally tally(program);
    if (!ruleCountsPath.empty()) options.ruleTally = &tally;
    auto finish = [&] {
        if (stages && !stagerecords::Dump(stagesDir, collector)) return 1;
        if (!ruleCountsPath.empty() && !WriteRuleCounts(ruleCountsPath, program, tally)) {
            std::fprintf(stderr, "hdtrans: cannot write %s\n", ruleCountsPath.c_str());
            return 1;
        }
        return 0;
    };

    if (!raw && !each) {
        std::vector<std::string> lines;
        size_t start = 0;
        for (;;) {
            const size_t end = input.find('\n', start);
            lines.push_back(input.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        hdTrans::internal::Memory memory;
        const std::vector<std::string> translated =
            hdTrans::internal::TranslateDocument(*snapshot, *generated, lines, memory, observer, options);
        for (size_t i = 0; i < translated.size(); i++) {
            if (stages) stagerecords::AddOutput(collector, static_cast<int>(i) + 1, translated[i]);
            if (i > 0) std::fputc('\n', stdout);
            std::fwrite(translated[i].data(), 1, translated[i].size(), stdout);
        }
        return finish();
    }
    size_t start = 0;
    while (start < input.size()) {
        size_t end = input.find('\n', start);
        if (end == std::string::npos) end = input.size();
        const std::string_view line(input.data() + start, end - start);
        std::string out;
        if (raw) {
            out = hdTrans::internal::TranslateRawLine(*snapshot, *generated, line, observer, options);
        } else {
            const std::string lines[] = {std::string(line)};
            hdTrans::internal::Memory memory;
            out = hdTrans::internal::TranslateDocument(*snapshot, *generated, lines, memory, observer, options).front();
        }
        if (stages) {
            stagerecords::AddOutput(collector, collector.caseBase + 1, out);
            collector.caseBase++;
        }
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fputc('\n', stdout);
        whyContext.lineBase++;
        start = end + 1;
    }
    return finish();
}
