#include "hdTrans/output/pieces.h"

#include "hdTrans/text/lines.h"

namespace hdTrans::output {

std::string String(const Pieces& p) {
    std::string s;
    for (const auto& x : p) s += x.text;
    return s;
}

void Add(Pieces& p, std::string_view text) {
    if (text.empty()) return;
    if (!p.empty() && p.back().span == SpanText) {
        p.back().text += text;
        return;
    }
    p.push_back({std::string(text), SpanText});
}

void Append(Pieces& p, const Pieces& q) {
    for (const auto& x : q) {
        if (x.span == SpanText) {
            Add(p, x.text);
        } else {
            p.push_back(x);
        }
    }
}

void Append(Pieces& p, Pieces&& q) {
    for (auto& x : q) {
        if (x.span != SpanText) {
            p.push_back(std::move(x));
        } else if (!x.text.empty() && (p.empty() || p.back().span != SpanText)) {
            p.push_back(std::move(x));
        } else {
            Add(p, x.text);
        }
    }
}

std::vector<Pieces> Lines(const Pieces& pieces) {
    std::vector<Pieces> out(1);
    for (const Piece& piece : pieces) {
        if (piece.span != SpanText) {
            out.back().push_back(piece);
            continue;
        }
        bool first = true;
        for (std::string_view line : text::Lines(piece.text)) {
            if (!first) out.emplace_back();
            Add(out.back(), line);
            first = false;
        }
    }
    return out;
}

Pieces JoinLines(const std::vector<Pieces>& lines) {
    Pieces out;
    for (std::size_t i = 0; i < lines.size(); i++) {
        if (i > 0) Add(out, "\n");
        Append(out, lines[i]);
    }
    return out;
}

std::string_view Head(const Pieces& p) {
    if (!p.empty() && p.front().span == SpanText) return p.front().text;
    return {};
}

void TrimRight(Pieces& p) {
    if (p.empty() || p.back().span != SpanText) return;
    auto& t = p.back().text;
    const size_t n = t.find_last_not_of(' ');
    if (n == std::string::npos) {
        p.pop_back();
    } else {
        t.resize(n + 1);
    }
}

void TrimLeft(Pieces& p) {
    if (p.empty() || p.front().span != SpanText) return;
    auto& t = p.front().text;
    const size_t n = t.find_first_not_of(' ');
    if (n == std::string::npos) {
        p.erase(p.begin());
    } else {
        t.erase(0, n);
    }
}

}  // namespace hdTrans::output
