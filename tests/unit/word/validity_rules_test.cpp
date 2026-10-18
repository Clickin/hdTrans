#include "hdTrans/word/validity_rules.h"

#include <string>

#include "fnv.h"
#include "hdtest.h"

using namespace hdTrans::word;

// The expected value is the hash of the rows the data was generated from, in order.
HD_TEST(validity_rules_fingerprint) {
    const auto rules = ValidityRules();
    hdtest::Fnv h;
    h.U32(static_cast<uint32_t>(rules.size()));
    for (const auto& r : rules) {
        h.Str(r.source);
        h.Byte(static_cast<uint8_t>(r.sourceClass));
        h.Byte(static_cast<uint8_t>(r.followClass));
        h.Str(r.first);
    }
    HD_CHECK_EQ(h.h, uint64_t{0x35a9c64d6de4dfafull});
    HD_CHECK_EQ(rules.size(), size_t{740});
    HD_REQUIRE(!rules.empty());
    HD_CHECK_EQ(std::string(rules[0].source), std::string("でも"));
    HD_CHECK_EQ(rules[0].sourceClass, '2');
    HD_CHECK_EQ(rules[0].followClass, 'X');
}
