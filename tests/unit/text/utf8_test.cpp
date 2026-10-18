#include "hdTrans/text/utf8.h"

#include "hdtest.h"

using hdTrans::text::AppendRune;
using hdTrans::text::DecodeRune;
using hdTrans::text::kReplacement;
using hdTrans::text::RuneCount;

HD_TEST(utf8_decode_round_trip) {
    struct Case {
        std::string_view in;
        char32_t cp;
        size_t size;
    };
    const Case cases[] = {
        {"a", 'a', 1},
        {"\xC3\xA9", 0xE9, 2},          // é
        {"\xEA\xB0\x80", 0xAC00, 3},    // 가
        {"\xE3\x81\x82", 0x3042, 3},    // あ
        {"\xF0\x9F\x98\x80", 0x1F600, 4},
    };
    for (const auto& c : cases) {
        const auto r = DecodeRune(c.in);
        HD_CHECK_EQ(static_cast<uint32_t>(r.cp), static_cast<uint32_t>(c.cp));
        HD_CHECK_EQ(r.size, c.size);
        std::string out;
        AppendRune(out, c.cp);
        HD_CHECK_EQ(out, std::string(c.in));
    }
}

HD_TEST(utf8_decode_invalid_consumes_one_byte) {
    const std::string_view cases[] = {
        "\x80",              // stray continuation
        "\xC3",              // truncated
        "\xE3\x81",          // truncated
        "\xC0\x80",          // overlong
        "\xED\xA0\x80",      // surrogate
        "\xF4\x90\x80\x80",  // > U+10FFFF
        "\xFF",
    };
    for (const auto& c : cases) {
        const auto r = DecodeRune(c);
        HD_CHECK_EQ(static_cast<uint32_t>(r.cp), static_cast<uint32_t>(kReplacement));
        HD_CHECK_EQ(r.size, size_t{1});
    }
    HD_CHECK_EQ(DecodeRune("").size, size_t{0});
}

HD_TEST(utf8_rune_count) {
    HD_CHECK_EQ(RuneCount(""), size_t{0});
    HD_CHECK_EQ(RuneCount("abc"), size_t{3});
    HD_CHECK_EQ(RuneCount("\xE7\x8C\xAB\xE3\x81\x8C\xE5\xA5\xBD\xE3\x81\x8D"), size_t{4});  // 猫が好き
    HD_CHECK_EQ(RuneCount("a\x80z"), size_t{3});
}
