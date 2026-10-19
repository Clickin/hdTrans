#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <utility>

#include "rulec/rulec.h"

namespace hdTrans::rulec {

namespace {

// The code point at s[i] and its byte length; a stray byte is one code point.
uint32_t Decode(std::string_view s, std::size_t i, std::size_t& n) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    n = c < 0x80 ? 1 : c >> 5 == 6 ? 2 : c >> 4 == 14 ? 3 : c >> 3 == 30 ? 4 : 1;
    if (i + n > s.size()) n = 1;
    if (n == 1) return c;
    uint32_t cp = c & (0x7f >> n);
    for (std::size_t k = 1; k < n; k++) cp = cp << 6 | (static_cast<unsigned char>(s[i + k]) & 0x3f);
    return cp;
}

// Terminal columns of a code point: 2 for East Asian wide and full-width forms.
int Width(uint32_t cp) {
    if (cp == '\t') return 1;
    if ((cp >= 0x1100 && cp <= 0x115f) || (cp >= 0x2e80 && cp <= 0xa4cf) || (cp >= 0xac00 && cp <= 0xd7a3) ||
        (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xfe30 && cp <= 0xfe4f) || (cp >= 0xff00 && cp <= 0xff60) ||
        (cp >= 0xffe0 && cp <= 0xffe6) || (cp >= 0x20000 && cp <= 0x3fffd))
        return 2;
    return 1;
}

int DisplayWidth(std::string_view s) {
    int w = 0;
    for (std::size_t i = 0, n = 0; i < s.size(); i += n) w += Width(Decode(s, i, n));
    return w;
}

const char* SeverityName(Severity s) {
    return s == Severity::Error ? "error" : s == Severity::Warning ? "warning" : "note";
}

}  // namespace

Source::Source(std::string n, std::string t) : name(std::move(n)), text(std::move(t)) {
    lineStarts.push_back(0);
    for (std::size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\n') lineStarts.push_back(static_cast<uint32_t>(i + 1));
    }
    files.push_back({0, name});
}

const std::string& Source::FileAt(uint32_t offset) const {
    std::size_t f = 0;
    while (f + 1 < files.size() && files[f + 1].start <= offset) f++;
    return files[f].name;
}

void Source::Position(uint32_t offset, int& line, int& column) const {
    auto lineOf = [&](uint32_t o) {
        return static_cast<int>(std::upper_bound(lineStarts.begin(), lineStarts.end(), o) - lineStarts.begin());
    };
    std::size_t f = 0;
    while (f + 1 < files.size() && files[f + 1].start <= offset) f++;
    line = lineOf(offset);
    const uint32_t start = lineStarts[static_cast<std::size_t>(line - 1)];
    line -= lineOf(files[f].start) - 1;
    column = 1;
    const std::string_view s(text);
    for (std::size_t i = start, n = 0; i < offset && i < s.size(); i += n, column++) Decode(s, i, n);
}

std::string_view Source::Line(uint32_t offset) const {
    const auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), offset);
    const std::size_t start = *(it - 1);
    std::size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    if (end > start && text[end - 1] == '\r') end--;
    return std::string_view(text).substr(start, end - start);
}

void Diagnostics::Error(uint32_t offset, uint32_t length, std::string message) {
    list.push_back({Severity::Error, offset, length, std::move(message), {}});
    errors++;
}

void Diagnostics::Warning(std::string flag, uint32_t offset, uint32_t length, std::string message) {
    list.push_back({Severity::Warning, offset, length, std::move(message), std::move(flag)});
    warnings++;
}

void Diagnostics::Note(uint32_t offset, uint32_t length, std::string message) {
    list.push_back({Severity::Note, offset, length, std::move(message), {}});
}

std::string Format(const Source& source, const Diagnostic& d) {
    int line = 0, column = 0;
    source.Position(d.offset, line, column);
    std::string out = source.FileAt(d.offset) + ":" + std::to_string(line) + ":" + std::to_string(column) + ": " +
                      SeverityName(d.severity) + ": " + d.message;
    if (!d.flag.empty()) out += " [-W" + d.flag + "]";
    out += "\n";
    const std::string_view text = source.Line(d.offset);
    const std::size_t start = d.offset - static_cast<std::size_t>(text.data() - source.text.data());
    // Long record lines are shown from a little before the span.
    std::size_t from = 0;
    if (DisplayWidth(text.substr(0, std::min(start, text.size()))) > 60) {
        from = start;
        int back = 0;
        while (from > 0 && back < 30) {
            from--;
            while (from > 0 && (static_cast<unsigned char>(text[from]) & 0xc0) == 0x80) from--;
            back++;
        }
    }
    std::string shown = from > 0 ? "..." : "";
    shown += text.substr(from);
    out += "    " + shown + "\n";
    const int pad = (from > 0 ? 3 : 0) + DisplayWidth(text.substr(from, start > from ? start - from : 0));
    const std::size_t spanEnd = std::min<std::size_t>(start + d.length, text.size());
    const int width = spanEnd > start ? DisplayWidth(text.substr(start, spanEnd - start)) : 1;
    out += "    " + std::string(static_cast<std::size_t>(pad), ' ') + "^" +
           std::string(static_cast<std::size_t>(std::max(0, width - 1)), '~') + "\n";
    return out;
}

std::string FormatRow(const Source& source, const Diagnostic& d) {
    int line = 0, column = 0;
    source.Position(d.offset, line, column);
    std::string file = source.FileAt(d.offset);
    if (const std::size_t slash = file.find_last_of('/'); slash != std::string::npos) file = file.substr(slash + 1);
    std::string kind = d.kind ? std::string(1, d.kind) : std::string();
    std::string message = d.message;
    std::replace(message.begin(), message.end(), '\t', ' ');
    return kind + "\t" + (d.index >= 0 ? std::to_string(d.index) : std::string()) + "\t" +
           (d.statement > 0 ? std::to_string(d.statement) : std::string()) + "\t" + file + "\t" + std::to_string(line) + "\t" +
           std::to_string(column) + "\t" + SeverityName(d.severity) + "\t" + d.flag + "\t" + message + "\n";
}

bool ReadRuleDir(const std::string& dir, std::string& text, std::vector<Source::File>& files, std::string& error) {
    std::error_code ec;
    std::vector<std::string> paths;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".txt") paths.push_back(it->path().string());
    }
    if (ec) {
        error = dir + ": " + ec.message();
        return false;
    }
    std::sort(paths.begin(), paths.end());
    std::string body;
    int locals = 0, globals = 0;
    for (const std::string& p : paths) {
        std::FILE* f = std::fopen(p.c_str(), "rb");
        if (f == nullptr) {
            error = p + ": cannot read";
            return false;
        }
        std::string t;
        char buf[1 << 16];
        for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) t.append(buf, n);
        std::fclose(f);
        for (std::size_t i = 0; i < t.size();) {
            if (t.compare(i, 2, "L ") == 0 && i + 2 < t.size() && t[i + 2] >= '0' && t[i + 2] <= '9') locals++;
            if (t.compare(i, 2, "G ") == 0 && i + 2 < t.size() && ((t[i + 2] >= '0' && t[i + 2] <= '9') || t[i + 2] == '-')) globals++;
            const std::size_t nl = t.find('\n', i);
            i = nl == std::string::npos ? t.size() : nl + 1;
        }
        files.push_back({static_cast<uint32_t>(body.size()), p});
        body += t;
    }
    const std::string header = "rules 1 " + std::to_string(locals) + " " + std::to_string(globals) + "\n";
    for (Source::File& f : files) f.start += static_cast<uint32_t>(header.size());
    files.insert(files.begin(), {0, dir + "/(header)"});
    text = header + body;
    return true;
}

}  // namespace hdTrans::rulec
