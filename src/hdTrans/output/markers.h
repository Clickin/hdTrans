#pragma once
// Ending-family rows of the three marker tables: a key such as "&A1@0" (marker &, $ or
// %, a two-character family name, '@', a form code '0'..'9', 'A'..'C') to the Korean
// stem text of that form. Keys are literal bytes; spelling and form membership are
// not normalized.
#include <span>
#include <string_view>

namespace hdTrans::output {

// The row of key in the &, $ and % table; "" when the table has no such key.
std::string_view SourceMarkerRow(std::string_view key);
std::string_view DollarMarkerRow(std::string_view key);
std::string_view PercentMarkerRow(std::string_view key);

// The first characters of the family names in the table of marker ('&', '$' or '%'),
// in byte order; "" for other markers.
std::string_view MarkerFamilyLetters(char marker);

namespace internal {

struct MarkerRow {
    const char* key;
    const char* text;
};

// Sorted by key, each key once, no empty text. Spans and row views have process lifetime.
std::span<const MarkerRow> SourceMarkerTable();
std::span<const MarkerRow> DollarMarkerTable();
std::span<const MarkerRow> PercentMarkerTable();

}  // namespace internal

}  // namespace hdTrans::output
