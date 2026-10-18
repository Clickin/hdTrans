#pragma once
// UTF-8 decoding and encoding of single code points. Invalid or truncated input
// decodes as U+FFFD and consumes one byte, so a scan always advances.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace hdTrans::text {

constexpr char32_t kReplacement = 0xFFFD;

struct Rune {
    char32_t cp;
    size_t size;  // bytes consumed, 1..4; 0 only when s is empty
};

Rune DecodeRune(std::string_view s);
void AppendRune(std::string& out, char32_t cp);
size_t RuneCount(std::string_view s);

}  // namespace hdTrans::text
