#include <algorithm>
#include <iterator>
#include <string_view>

#include "core/word/analyze.h"
#include "hdtest.h"

HD_TEST(word_improvements_defaults_and_reference) {
    using hdTrans::word::Improvements;
    using hdTrans::word::Settings;
    constexpr std::string_view defaultOn[] = {
        "extraBuiltin", "literalCleanup", "imperativeEu", "causativePassive", "exclamationNa", "dialectForms",
        "expandedGroups", "semanticAlternatives", "nonterminalBoundary",
    };
    const Improvements defaults;
    const Improvements reference = Improvements::Reference();
    const Settings defaultSettings;
    const Settings referenceSettings = Settings::Reference();
#define HDTRANS_CHECK_IMPROVEMENT(name, defaultValue) { \
    const bool enabled = std::find(std::begin(defaultOn), std::end(defaultOn), std::string_view(#name)) != \
                         std::end(defaultOn); \
    HD_CHECK_EQ(defaults.name, enabled); \
    HD_CHECK_EQ(defaultSettings.improvements.name, enabled); \
    HD_CHECK(!reference.name); \
    HD_CHECK(!referenceSettings.improvements.name); \
}
    HDTRANS_IMPROVEMENTS(HDTRANS_CHECK_IMPROVEMENT)
#undef HDTRANS_CHECK_IMPROVEMENT
    HD_CHECK(referenceSettings.sessionSenses == nullptr);
}
