#pragma once
#include <cstddef>
#include <string_view>
#include <vector>

namespace hdTrans::text {

// Keeps empty lines, including the empty line after a final newline.
std::vector<std::string_view> Lines(std::string_view text);

constexpr std::size_t kMaxLineBytes = 64 * 1024;

// Ordinary lines are unchanged. Oversized lines use smaller analysis windows,
// preferring sentence ends, then spaces, then a complete code point.
std::vector<std::string_view> TranslationPieces(std::string_view line);

}  // namespace hdTrans::text
