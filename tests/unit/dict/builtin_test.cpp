#include "hdTrans/dict/builtin.h"

#include <string>

#include "fnv.h"
#include "hdtest.h"

using namespace hdTrans::dict;

// The expected value is the hash of the folder the data was generated from: the file
// count, then each file's name and text in name order.
HD_TEST(builtin_fingerprint) {
    const auto names = BuiltinNames();
    hdtest::Fnv h;
    h.U32(static_cast<uint32_t>(names.size()));
    for (const std::string_view name : names) {
        h.Str(name);
        h.Str(BuiltinText(name));
    }
    HD_CHECK_EQ(names.size(), std::size_t{6});
    HD_CHECK_EQ(h.h, uint64_t{0x3332ecc20204ada3ull});
    HD_CHECK(BuiltinText("none.tsv").empty());
    HD_CHECK(BuiltinText("endings.tsv").starts_with("// Builtin end rows."));
}
