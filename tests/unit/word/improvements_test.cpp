#include "hdTrans/word/analyze.h"
#include "hdtest.h"

HD_TEST(word_improvements_defaults_and_reference) {
    using hdTrans::word::Improvements;
    using hdTrans::word::Settings;
    const Improvements defaults;
    const Improvements reference = Improvements::Reference();
    const Settings defaultSettings;
    const Settings referenceSettings = Settings::Reference();
#define HDTRANS_CHECK_IMPROVEMENT(name, defaultValue) \
    HD_CHECK_EQ(defaults.name, defaultValue); \
    HD_CHECK_EQ(defaultSettings.improvements.name, defaultValue); \
    HD_CHECK(!reference.name); \
    HD_CHECK(!referenceSettings.improvements.name);
    HDTRANS_IMPROVEMENTS(HDTRANS_CHECK_IMPROVEMENT)
#undef HDTRANS_CHECK_IMPROVEMENT
    HD_CHECK(referenceSettings.sessionSenses == nullptr);
}
