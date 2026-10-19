#include "core/output/markers.h"

#include <algorithm>
#include <array>
#include <bit>
#include <string>
#include <vector>

#include "core/output/inflect.h"

namespace hdTrans::output {
namespace {

struct CompiledRow {
    std::string key;
    std::string surface;
};

struct MarkerTables {
    std::vector<CompiledRow> storage;
    std::array<std::vector<internal::MarkerRow>, 3> rows;
};

const MarkerTables& Tables() {
    static const auto tables = [] {
        MarkerTables result;
        size_t count = 0;
        for (const auto& family : internal::KoreanFamilies()) count += std::popcount(family.forms);
        result.storage.reserve(count);
        for (const auto& family : internal::KoreanFamilies()) {
            for (const char code : std::string_view("0123456789ABC")) {
                if (!(family.forms & internal::InflectionFormBit(code))) continue;
                result.storage.push_back({std::string(family.key) + code, internal::Inflect(family, code)});
            }
        }
        // Publish views only after their backing strings are in their final locations.
        for (const auto& row : result.storage) {
            const size_t table = row.key[0] == '&' ? 0 : row.key[0] == '$' ? 1 : 2;
            result.rows[table].push_back({row.key.c_str(), row.surface.c_str()});
        }
        return result;
    }();
    return tables;
}

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

namespace internal {

std::span<const MarkerRow> SourceMarkerTable() { return Tables().rows[0]; }
std::span<const MarkerRow> DollarMarkerTable() { return Tables().rows[1]; }
std::span<const MarkerRow> PercentMarkerTable() { return Tables().rows[2]; }

}  // namespace internal
}  // namespace hdTrans::output
