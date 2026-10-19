#include <string>

#include "core/names/registry.h"
#include "hdtest.h"

HD_TEST(names_occurrence_guards_ignore_plain_endings) {
    using namespace hdTrans;
    for (const bool lexical : {false, true}) {
        const std::string rows = std::string("key\tko\tpos\tid\n") +
            (lexical ? "やな\t야나\tP200\t1\n" : "");
        const auto loaded = dict::Load({{0, "base.tsv", rows}});
        HD_REQUIRE(loaded.snapshot != nullptr && loaded.errors.empty());
        const auto& base = loaded.snapshot->Plain();
        const auto& extra = loaded.snapshot->Plain(true);
        HD_REQUIRE(extra.Entries().size() > base.Entries().size());
        bool extraEnding = false;
        for (std::size_t i = base.Entries().size(); i < extra.Entries().size(); i++) {
            const auto& entry = extra.Entries()[i];
            HD_CHECK(entry.end);
            if (entry.key == "やな") extraEnding = true;
        }
        HD_REQUIRE(extraEnding);
        names::Registry registry(loaded.snapshot.get());
        registry.Add("[name text=\"や\"]");
        HD_REQUIRE(registry.Named("や"));
        struct Case {
            const char* line;
            bool noun;
        };
        const Case cases[] = {{"やな", lexical}, {"やから", false}, {"や", false}};
        for (const auto& test : cases) {
            const auto occurrences = registry.Occurrences(test.line);
            HD_REQUIRE(occurrences.size() == 1);
            const auto& occurrence = occurrences[0];
            HD_CHECK_EQ(occurrence.surface, std::string("や"));
            HD_CHECK_EQ(occurrence.start, std::size_t{0});
            HD_CHECK_EQ(occurrence.end, std::string("や").size());
            HD_CHECK_EQ(occurrence.noun, test.noun);
        }
    }
}
