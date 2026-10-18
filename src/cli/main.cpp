// hdtrans: reads UTF-8 from stdin (or -in FILE) and writes the translation to stdout. The
// whole input is one session unless -each or -raw is given.
//   -dicts DIR[,DIR...]   dictionary folders in load order (default: dicts/base next to the executable)
//   -each                 a new session for every line
//   -raw                  translate each line with the word pipeline only
//   -reference            disable translation improvements
//   -verify-dicts         verify source hashes and every cache segment
//   -why                  write word choices and shadow scores to stderr
//   -select-shadow        score shortest targets without changing the translation
//   -in FILE              read the input from FILE
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "hdTrans/engine.h"
#include "hdTrans/engine/driver.h"
#include "hdTrans/word/select.h"

namespace {

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

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // The output bytes are the same on every platform: no text-mode CRLF translation.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    std::string dicts = "dicts/base";
    std::string inPath;
    bool raw = false;
    bool each = false;
    bool why = false;
    bool selectShadow = false;
    bool reference = false;
    bool verifyDicts = false;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!std::strcmp(a, "-dicts") && i + 1 < argc) dicts = argv[++i];
        else if (!std::strcmp(a, "-in") && i + 1 < argc) inPath = argv[++i];
        else if (!std::strcmp(a, "-raw")) raw = true;
        else if (!std::strcmp(a, "-each")) each = true;
        else if (!std::strcmp(a, "-reference")) reference = true;
        else if (!std::strcmp(a, "-verify-dicts")) verifyDicts = true;
        else if (!std::strcmp(a, "-why")) why = true;
        else if (!std::strcmp(a, "-select-shadow")) selectShadow = true;
        else {
            std::fprintf(stderr, "usage: hdtrans [-dicts DIR,...] [-each | -raw] [-reference] [-verify-dicts] [-why] [-select-shadow] [-in FILE]\n");
            return 2;
        }
    }

    if (why) std::setvbuf(stderr, nullptr, _IOFBF, 1 << 16);

    hdTrans::Engine engine(reference);
    if (!engine.Open(SplitComma(dicts), verifyDicts)) {
        std::fprintf(stderr, "hdtrans: no dictionary loaded from %s\n", dicts.c_str());
        return 1;
    }

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
    hdTrans::internal::TranslationOptions options;
    options.reference = reference;
    if (selectShadow) {
        options.scorers = diagnosticScorers;
        options.selectShadow = true;
    }
    WhyContext whyContext;
    hdTrans::internal::StageObserver observer;
    if (why) {
        observer.context = &whyContext;
        observer.line = WhyLine;
        observer.resolved = WhyResolved;
    }

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
            if (i > 0) std::fputc('\n', stdout);
            std::fwrite(translated[i].data(), 1, translated[i].size(), stdout);
        }
        return 0;
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
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fputc('\n', stdout);
        whyContext.lineBase++;
        start = end + 1;
    }
    return 0;
}
