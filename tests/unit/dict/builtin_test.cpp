#include "core/dict/builtin.h"

#include <string>

#include "hdtest.h"

using namespace hdTrans::dict;

HD_TEST(builtin_unknown_file) {
    HD_CHECK(BuiltinText("none.tsv").empty());
    HD_CHECK(BuiltinText("none.tsv", true).empty());
    HD_CHECK(ExtraBuiltinText("none.tsv").empty());
}

HD_TEST(builtin_extra_isolation) {
    const std::string row = "駄目で元々\t밑져야 본전\tphrase\n";
    HD_CHECK(BuiltinText("phrases.tsv").find(row) == std::string::npos);
    HD_CHECK(BuiltinText("phrases.tsv", true).find(row) != std::string::npos);
    HD_CHECK(ExtraBuiltinText("phrases.tsv").find(row) != std::string::npos);
    HD_CHECK(BuiltinText("none.tsv", true).empty());
    HD_CHECK(BuiltinText("phrases.tsv").find(row) == std::string::npos);
}
