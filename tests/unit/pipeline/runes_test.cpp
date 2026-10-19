#include "core/pipeline/runes.h"

#include <cstdint>
#include <string_view>

#include "hdtest.h"

using namespace hdTrans::pipeline::internal;

HD_TEST(pipeline_last_rune_widths_and_invalid_tails) {
    const struct {
        std::string_view text;
        char32_t last;
        size_t bytes;
        bool valid;
    } cases[] = {
        {"", 0xfffd, 0, true}, {"A", U'A', 1, true}, {"가Z", U'Z', 1, true},
        {std::string_view("a\0", 2), 0, 1, true}, {"a\x7f", 0x7f, 1, true},
        {"¢", 0xa2, 2, true}, {"a¢", 0xa2, 2, true}, {"가", U'가', 3, true},
        {"x한", U'한', 3, true}, {"😀", 0x1f600, 4, true}, {"한😀", 0x1f600, 4, true},
        {"𠀀", 0x20000, 4, true}, {"x𠀀", 0x20000, 4, true},
        {"a\xf4\x8f\xbf\xbf", 0x10ffff, 4, true}, {"a\xef\xbf\xbd", 0xfffd, 3, true},
        {"\x80", 0xfffd, 1, false}, {"a\xbf", 0xfffd, 1, false},
        {"a\xc2", 0xfffd, 1, false}, {"a\xe3\x81", 0xfffd, 1, false},
        {"a\xf0\x9f\x98", 0xfffd, 1, false}, {"a\xf0\x9f\x98\x80\x80", 0xfffd, 1, false},
        {"a\xc0\xaf", 0xfffd, 1, false}, {"a\xed\xa0\x80", 0xfffd, 1, false},
        {"a\xf4\x90\x80\x80", 0xfffd, 1, false}, {"\xff😀", 0x1f600, 4, false},
        {"\xffZ", U'Z', 1, false},
    };
    for (const auto& c : cases) {
        const auto rune = LastRune(c.text);
        HD_CHECK_EQ(static_cast<uint32_t>(rune.cp), static_cast<uint32_t>(c.last));
        HD_CHECK_EQ(rune.size, c.bytes);
        HD_CHECK_EQ(ValidUtf8(c.text), c.valid);
    }
}
