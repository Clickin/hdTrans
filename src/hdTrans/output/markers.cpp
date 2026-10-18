#include "hdTrans/output/markers.h"

#include <algorithm>

namespace hdTrans::output {

namespace {

std::string_view Find(std::span<const internal::MarkerRow> table, std::string_view key) {
    const auto it = std::lower_bound(table.begin(), table.end(), key,
                                     [](const internal::MarkerRow& r, std::string_view k) { return r.key < k; });
    if (it == table.end() || it->key != key) return {};
    return it->text;
}

}  // namespace

std::string_view SourceMarkerRow(std::string_view key) { return Find(internal::SourceMarkerTable(), key); }
std::string_view DollarMarkerRow(std::string_view key) { return Find(internal::DollarMarkerTable(), key); }
std::string_view PercentMarkerRow(std::string_view key) { return Find(internal::PercentMarkerTable(), key); }

}  // namespace hdTrans::output
