#include <algorithm>

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
}

void Source::Position(uint32_t offset, int& line, int& column) const {
    const auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), offset);
    line = static_cast<int>(it - lineStarts.begin());
    const uint32_t start = lineStarts[static_cast<std::size_t>(line - 1)];
    column = 1;
    const std::string_view s(text);
    for (std::size_t i = start, n = 0; i < offset && i < s.size(); i += n, column++) Decode(s, i, n);
}

std::string_view Source::Line(int line) const {
    const std::size_t start = lineStarts[static_cast<std::size_t>(line - 1)];
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
    std::string out = source.name + ":" + std::to_string(line) + ":" + std::to_string(column) + ": " +
                      SeverityName(d.severity) + ": " + d.message;
    if (!d.flag.empty()) out += " [-W" + d.flag + "]";
    out += "\n";
    const std::string_view text = source.Line(line);
    const std::size_t start = d.offset - source.lineStarts[static_cast<std::size_t>(line - 1)];
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
    std::string kind = d.kind ? std::string(1, d.kind) : std::string();
    std::string message = d.message;
    std::replace(message.begin(), message.end(), '\t', ' ');
    return kind + "\t" + (d.index >= 0 ? std::to_string(d.index) : std::string()) + "\t" +
           (d.statement > 0 ? std::to_string(d.statement) : std::string()) + "\t" + std::to_string(line) + "\t" +
           std::to_string(column) + "\t" + SeverityName(d.severity) + "\t" + d.flag + "\t" + message + "\n";
}

}  // namespace hdTrans::rulec
