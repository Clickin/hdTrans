#pragma once
// FNV-1a 64 for the identity tests of generated tables: integers little endian,
// strings as a u32 length and their bytes.
#include <cstdint>
#include <string_view>

namespace hdtest {

struct Fnv {
    uint64_t h = 14695981039346656037ull;

    void Byte(uint8_t b) { h = (h ^ b) * 1099511628211ull; }
    void U32(uint32_t v) {
        for (int i = 0; i < 4; i++) Byte(static_cast<uint8_t>(v >> (8 * i)));
    }
    void Str(std::string_view s) {
        U32(static_cast<uint32_t>(s.size()));
        for (char c : s) Byte(static_cast<uint8_t>(c));
    }
};

}  // namespace hdtest
