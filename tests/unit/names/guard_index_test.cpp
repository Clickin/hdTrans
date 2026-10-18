#include <string>
#include <vector>

#include "hdTrans/dict/snapshot.h"
#include "hdtest.h"

HD_TEST(names_guard_general_index_keeps_base_candidates) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv",
        "key\tko\tpos\tid\n"
        "や\t야\tP200\t1\n"
        "やな\t야나\tP200\t2\n"
        "だなあ\t다나아\tP200\t3\n"
        "じゃけえ\t자케에\tP200\t4\n"}});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const auto& base = loaded.snapshot->Plain();
    const auto& extra = loaded.snapshot->Plain(true);
    HD_CHECK(extra.Entries().size() > base.Entries().size());
    for (std::size_t i = base.Entries().size(); i < extra.Entries().size(); i++) HD_CHECK(extra.Entries()[i].end);
    int checked = 0;
    // Name guards ignore plain entries; ending entries do not hide base candidates.
    for (const auto& entry : extra.Entries()) {
        std::vector<std::string> rows[2];
        for (int mode = 0; mode < 2; mode++) {
            dict::LookupContext context;
            context.plain = mode == 0 ? &base : &extra;
            const auto candidates = loaded.snapshot->GetWordInfo(entry.key, dict::LookupMode::General, context);
            for (const auto& candidate : candidates.list) {
                if (candidate.user != nullptr || candidate.codes == nullptr) continue;
                rows[mode].push_back(std::string(candidate.source) + '\t' + std::string(candidate.codes->part.Text()) +
                                     '\t' + std::string(candidate.codes->sem.Text()));
            }
        }
        if (!rows[0].empty()) checked++;
        HD_CHECK(rows[0] == rows[1]);
    }
    HD_CHECK(checked > 0);
}
