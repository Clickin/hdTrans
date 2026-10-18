#include <string>

#include "hdTrans/engine/driver.h"
#include "hdtest.h"

namespace {

int scoreCalls = 0;

int PreferFirst(const hdTrans::word::Item&, int candidate, const hdTrans::word::SelectContext&) {
    scoreCalls++;
    return candidate == 0 ? 1 : 0;
}

void CountShadows(void* context, int, int, const hdTrans::word::Chain& chain) {
    auto& count = *static_cast<std::size_t*>(context);
    for (const auto* item = chain.head; item != nullptr; item = item->next) count += item->shadowChoices.size();
}

}  // namespace

HD_TEST(engine_reference_disables_selection_options) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv",
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "東京\t동경\tN130\tQPNXXX\t\tGN\t0\t1\n"
        "東京\t도쿄\tN130\tQCT\t\tGN\t9\t2\n"}});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto generated = internal::NewGenerated(*loaded.snapshot);
    const word::Scorer scorers[] = {{"first", PreferFirst}};
    internal::TranslationOptions options;
    options.reference = true;
    options.scorers = scorers;
    options.selectShadow = true;
    options.selectRules = true;
    options.wordImprovements.documentSenses = true;
    std::size_t shadows = 0;
    internal::StageObserver observer;
    observer.context = &shadows;
    observer.resolved = CountShadows;
    internal::Memory memory;
    const std::string input[] = {"東京、", "東京。"};
    scoreCalls = 0;
    const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, input, memory, observer, options);
    HD_REQUIRE(out.size() == 2);
    HD_CHECK_EQ(out[0], std::string("도쿄,"));
    HD_CHECK_EQ(out[1], std::string("동경."));
    HD_CHECK(memory.senses.choices.empty());
    internal::TranslationOptions plain;
    plain.reference = true;
    const auto expectedRaw = internal::TranslateRawLine(*loaded.snapshot, *generated, input[0], {}, plain);
    HD_CHECK(!expectedRaw.empty());
    HD_CHECK_EQ(internal::TranslateRawLine(*loaded.snapshot, *generated, input[0], observer, options), expectedRaw);
    HD_CHECK_EQ(scoreCalls, 0);
    HD_CHECK_EQ(shadows, std::size_t(0));

    options.reference = false;
    options.selectShadow = false;
    internal::Memory active;
    const auto changed = internal::TranslateDocument(*loaded.snapshot, *generated, input, active, {}, options);
    HD_REQUIRE(changed.size() == 2);
    HD_CHECK_EQ(changed[0], std::string("동경,"));
    HD_CHECK(scoreCalls > 0);
}
