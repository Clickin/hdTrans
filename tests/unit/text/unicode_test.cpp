#include "hdTrans/text/unicode.h"

#include <cstdint>

#include "hdtest.h"

using namespace hdTrans::text;

namespace {

// FNV-1a over the membership bit of every code point 0..U+10FFFF; the expected values
// are those of the tables the ranges were generated from.
uint64_t Fingerprint(bool (*pred)(char32_t)) {
    uint64_t h = 14695981039346656037ull;
    for (char32_t r = 0; r <= 0x10FFFF; r++) {
        h = (h ^ (pred(r) ? 1u : 0u)) * 1099511628211ull;
    }
    return h;
}

}  // namespace

HD_TEST(unicode_fingerprints) {
    struct Case {
        const char* name;
        bool (*pred)(char32_t);
        uint64_t want;
    };
    const Case cases[] = {
        {"Kanji", IsKanji, 0x09dff0a29d41226cull},
        {"ClosePunctuation", IsClosePunctuation, 0xfbb75a7c428ae2e4ull},
        {"OtherSymbol", IsOtherSymbol, 0xcbc92420e6f2342full},
        {"Letter", IsLetter, 0x922305fde0a6d1e9ull},
        {"Punct", IsPunct, 0x4279a259d8baff7bull},
        {"Symbol", IsSymbol, 0xb4761575c09ebdd8ull},
        {"Digit", IsDigit, 0xf16f4246f8464d23ull},
        {"Space", IsSpace, 0x05d570bb8c3ec7acull},
    };
    for (const auto& c : cases) {
        const uint64_t got = Fingerprint(c.pred);
        if (got != c.want) hdtest::Fail(__FILE__, __LINE__, std::string("fingerprint of ") + c.name);
    }
}

HD_TEST(unicode_samples) {
    HD_CHECK(IsKanji(U'猫'));
    HD_CHECK(IsKanji(U'々'));  // script Han includes the iteration mark
    HD_CHECK(IsKanji(U'あ') == false);
    HD_CHECK(IsLetter(U'あ'));
    HD_CHECK(IsLetter(U'가'));
    HD_CHECK(IsLetter(U'。') == false);
    HD_CHECK(IsPunct(U'。'));
    HD_CHECK(IsPunct(U'「'));
    HD_CHECK(IsClosePunctuation(U'」'));
    HD_CHECK(IsClosePunctuation(U'「') == false);
    HD_CHECK(IsSymbol(U'♪'));
    HD_CHECK(IsOtherSymbol(U'♪'));
    HD_CHECK(IsOtherSymbol(U'＋') == false);
    HD_CHECK(IsDigit(U'３'));
    HD_CHECK(IsDigit(U'三') == false);
    HD_CHECK(IsSpace(U'　'));
    HD_CHECK(IsSpace(0x85));
    HD_CHECK(IsSpace(U'a') == false);
}
