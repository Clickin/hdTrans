#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace hdTrans::gui {

enum class TextEncoding { Utf8, Utf8Bom, Utf16Le, ShiftJis };

// BOMs take precedence over UTF-8 and CP932. Decoded text has LF newlines and no NULs.
bool DecodeText(std::string_view bytes, std::string& utf8, TextEncoding& encoding);
bool ReadTextFile(const std::filesystem::path& path, std::string& utf8, TextEncoding& encoding);
// Writes validated UTF-8 with a BOM, preserving the supplied newlines.
bool WriteTextFile(const std::filesystem::path& path, std::string_view utf8);

}  // namespace hdTrans::gui
