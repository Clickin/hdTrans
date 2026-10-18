#include "hdTrans/output/text.h"

#include "hdTrans/output/particle.h"
#include "hdTrans/text/hangul.h"
#include "hdTrans/text/utf8.h"

namespace hdTrans::output::internal {

int Width(char32_t r) { return r < 0x80 || (r >= 0xFF61 && r <= 0xFF9F) ? 1 : 2; }

int WidthOf(std::string_view s) {
    int n = 0;
    for (size_t i = 0; i < s.size();) {
        if (static_cast<unsigned char>(s[i]) < 0x80) {
            n++;
            i++;
            continue;
        }
        const auto r = text::DecodeRune(s.substr(i));
        n += Width(r.cp);
        i += r.size;
    }
    return n;
}

Cut CutLast(std::string_view s, int n) {
    size_t i = s.size();
    int k = 0;
    while (i > 0 && k < n && static_cast<unsigned char>(s[i - 1]) < 0x80) {
        i--;
        k++;
    }
    if (k == n) return {s.substr(0, i), s.substr(i)};
    i = s.size();
    const TextBefore t(s);
    for (k = 0; i > 0 && k < n / 2; k++) i -= DecodeLastRune(t, i).size;
    return {s.substr(0, i), s.substr(i)};
}

Cut FirstByte(std::string_view s) {
    if (!s.empty() && static_cast<unsigned char>(s[0]) < 0x80) return {s.substr(0, 1), s.substr(1)};
    return {{}, s};
}

std::string_view SkipWidth(std::string_view s, int n) {
    size_t i = 0;
    int w = 0;
    while (i < s.size() && w < n) {
        const auto r = text::DecodeRune(s.substr(i));
        w += Width(r.cp);
        i += r.size;
    }
    return s.substr(i);
}

Cut CutFirst(std::string_view s, int n) {
    size_t i = 0;
    int w = 0;
    while (i < s.size()) {
        const auto r = text::DecodeRune(s.substr(i));
        if (w + Width(r.cp) > n) break;
        w += Width(r.cp);
        i += r.size;
    }
    return {s.substr(0, i), s.substr(i)};
}

std::string_view MidWidth(std::string_view s, int start, int count) { return CutFirst(SkipWidth(s, start), count).head; }

bool HasSyllable(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        const auto r = text::DecodeRune(s.substr(i));
        if (text::Final(r.cp) >= 0) return true;
        i += r.size;
    }
    return false;
}

}  // namespace hdTrans::output::internal
