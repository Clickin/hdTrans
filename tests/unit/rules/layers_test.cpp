// The layer table of docs/pipeline-layers.md lists the execution layers in the order the code
// runs them: in each function named by the code column, the calls of its rows come in the
// order of the rows.
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "hdtest.h"

namespace {

std::string RepoDir() {
    std::string f = __FILE__;
    for (int i = 0; i < 4; i++) {
        const std::size_t slash = f.find_last_of("/\\");
        if (slash == std::string::npos) return ".";
        f.resize(slash);
    }
    return f;
}

bool Read(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[1 << 16];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
    std::fclose(f);
    return true;
}

std::vector<std::string> Split(std::string_view s, char sep) {
    std::vector<std::string> out;
    for (std::size_t start = 0;;) {
        const std::size_t end = s.find(sep, start);
        out.emplace_back(s.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) return out;
        start = end + 1;
    }
}

// The body of the definition of function: from its line starting in column 0 to the next
// line that is a lone "}".
std::string_view Body(std::string_view text, const std::string& function) {
    for (std::size_t at = text.find(function + "("); at != std::string_view::npos; at = text.find(function + "(", at + 1)) {
        const std::size_t line = text.rfind('\n', at) + 1;
        if (text[line] == ' ' || text[line] == '\t') continue;
        const char before = at > 0 ? text[at - 1] : ' ';
        if (std::isalnum(static_cast<unsigned char>(before)) || before == '_' || before == ':') continue;
        const std::size_t end = text.find("\n}\n", at);
        if (end == std::string_view::npos) return {};
        return text.substr(at, end - at);
    }
    return {};
}

}  // namespace

// The code cell of a table row: the backquoted spans of the last cell (file, function, call).
std::vector<std::string> CodeCell(std::string_view row) {
    std::vector<std::string> out;
    const std::size_t end = row.rfind('|');
    const std::size_t start = row.rfind('|', end - 1);
    if (end == std::string_view::npos || start == std::string_view::npos) return out;
    const std::string_view cell = row.substr(start + 1, end - start - 1);
    for (std::size_t open = cell.find('`'); open != std::string_view::npos; open = cell.find('`', open + 1)) {
        const std::size_t close = cell.find('`', open + 1);
        if (close == std::string_view::npos) break;
        out.emplace_back(cell.substr(open + 1, close - open - 1));
        open = close;
    }
    return out;
}

HD_TEST(rules_layers_order) {
    std::string doc;
    HD_REQUIRE(Read(RepoDir() + "/docs/pipeline-layers.md", doc));
    std::map<std::string, std::string> files;
    std::map<std::string, std::size_t> next;  // file|function -> search position in the body
    int rows = 0;
    bool table = false;
    for (const std::string& line : Split(doc, '\n')) {
        if (line.starts_with("| 층 |")) {
            table = true;
            continue;
        }
        if (!table || line.starts_with("|---")) continue;
        if (!line.starts_with("| ")) {
            if (rows > 0) break;
            continue;
        }
        const std::string layer = line.substr(2, line.find(" |", 2) - 2);
        const std::vector<std::string> code = CodeCell(line);
        if (code.size() != 3) {
            hdtest::Fail(__FILE__, __LINE__, "layer " + layer + ": the code cell needs a file, a function and a call");
            continue;
        }
        std::string& text = files[code[0]];
        if (text.empty()) HD_REQUIRE(Read(RepoDir() + "/" + code[0], text));
        const std::string_view body = Body(text, code[1]);
        if (body.empty()) {
            hdtest::Fail(__FILE__, __LINE__, "layer " + layer + ": no function " + code[1] + " in " + code[0]);
            continue;
        }
        std::size_t& from = next[code[0] + "|" + code[1]];
        const std::size_t at = body.find(code[2], from);
        if (at == std::string_view::npos)
            hdtest::Fail(__FILE__, __LINE__, "layer " + layer + ": " + code[1] + " does not call " + code[2] +
                                                 " after the layer before it; the code order differs from docs/pipeline-layers.md");
        else
            from = at + code[2].size();
        rows++;
    }
    HD_CHECK(rows >= 20);
}
