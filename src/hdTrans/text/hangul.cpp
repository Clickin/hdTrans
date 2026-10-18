#include "hdTrans/text/hangul.h"

#include <string>

#include "hdTrans/text/utf8.h"

namespace hdTrans::text {
namespace {

// Each initial mapped to its plain consonant.
constexpr int kPlainInitial[19] = {0, 0, 2, 3, 3, 5, 6, 7, 7, 9, 9, 11, 12, 12, 12, 0, 3, 7, 18};

// ㅑ ㅕ ㅛ ㅠ to ㅏ ㅓ ㅗ ㅜ.
constexpr int PlainVowel(int v) {
    switch (v) {
        case 2: return 0;
        case 6: return 4;
        case 12: return 8;
        case 17: return 13;
    }
    return 0;
}

std::string SoundKey(std::string_view s) {
    std::string out;
    int prev = -1;  // vowel of the last syllable when it has no final
    while (!s.empty()) {
        const Rune r = DecodeRune(s);
        s.remove_prefix(r.size);
        const auto syl = Split(r.cp);
        if (!syl) {
            AppendRune(out, r.cp);
            prev = -1;
            continue;
        }
        int i = kPlainInitial[syl->initial];
        int v = syl->vowel;
        const int f = syl->final;
        if (i == 9 && v == 18) {
            i = 12;
        } else if (i == 12 && (v == 2 || v == 6 || v == 12 || v == 17)) {
            v = PlainVowel(v);
        } else if (v == 1) {
            v = 5;
        }
        if (i == 11 && f == 0 && prev >= 0 &&
            (v == prev || (v == 13 && (prev == 8 || prev == 12 || prev == 17)) || (v == 20 && prev == 5))) {
            continue;
        }
        AppendRune(out, Join(i, v, f));
        prev = v;
        if (f != 0) prev = -1;
    }
    return out;
}

}  // namespace

bool SameSound(std::string_view a, std::string_view b) { return SoundKey(a) == SoundKey(b); }

}  // namespace hdTrans::text
