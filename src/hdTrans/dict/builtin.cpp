#include "hdTrans/dict/builtin.h"

namespace hdTrans::dict {

std::vector<std::string_view> BuiltinNames() {
    std::vector<std::string_view> out;
    for (const internal::BuiltinTable& t : internal::BuiltinTables()) out.push_back(t.name);
    return out;
}

std::string BuiltinText(std::string_view name) {
    std::string out;
    for (const internal::BuiltinTable& t : internal::BuiltinTables()) {
        if (t.name != name) continue;
        std::size_t size = 0;
        for (const std::string_view line : t.lines) size += line.size();
        out.reserve(size);
        for (const std::string_view line : t.lines) out += line;
    }
    return out;
}

}  // namespace hdTrans::dict
