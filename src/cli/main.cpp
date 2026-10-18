// hdtrans: reads UTF-8 from stdin (or -in FILE) and writes the translation to stdout. The
// whole input is one session unless -each or -raw is given.
//   -dicts DIR[,DIR...]   dictionary folders in load order (default: dicts/base next to the executable)
//   -each                 a new session for every line
//   -raw                  translate each line with the word pipeline only
//   -in FILE              read the input from FILE
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hdTrans/engine.h"
#include "hdTrans/engine/raw.h"

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

}  // namespace

int main(int argc, char** argv) {
    std::string dicts = "dicts/base";
    std::string inPath;
    bool raw = false;
    bool each = false;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!std::strcmp(a, "-dicts") && i + 1 < argc) dicts = argv[++i];
        else if (!std::strcmp(a, "-in") && i + 1 < argc) inPath = argv[++i];
        else if (!std::strcmp(a, "-raw")) raw = true;
        else if (!std::strcmp(a, "-each")) each = true;
        else {
            std::fprintf(stderr, "usage: hdtrans [-dicts DIR,...] [-each | -raw] [-in FILE]\n");
            return 2;
        }
    }

    hdTrans::Engine engine;
    if (!engine.Open(SplitComma(dicts))) {
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

    if (!raw && !each) {
        hdTrans::Session session(engine);
        const std::string out = session.Translate(input);
        std::fwrite(out.data(), 1, out.size(), stdout);
        return 0;
    }
    size_t start = 0;
    while (start < input.size()) {
        size_t end = input.find('\n', start);
        if (end == std::string::npos) end = input.size();
        std::string_view line(input.data() + start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const std::string out = hdTrans::internal::TranslateRaw(engine, line);
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fputc('\n', stdout);
        start = end + 1;
    }
    return 0;
}
