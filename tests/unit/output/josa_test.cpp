#include "hdTrans/output/josa.h"

#include <string>

#include "hdtest.h"

HD_TEST(output_josa_closing_brackets) {
    const char* brackets[] = {"〕", "〉", "》", "」", "』", "】"};
    const struct { const char* word; const char* pattern; const char* form; } cases[] = {
        {"집", "(이)가", "이"}, {"집", "(은)는", "은"}, {"집", "(을)를", "을"}, {"집", "(으)로", "으로"},
        {"나무", "(이)가", "가"}, {"나무", "(은)는", "는"}, {"나무", "(을)를", "를"}, {"나무", "(으)로", "로"},
        {"길", "(이)가", "이"}, {"길", "(은)는", "은"}, {"길", "(을)를", "을"}, {"길", "(으)로", "로"},
    };
    for (const auto* bracket : brackets) {
        for (const auto& c : cases) {
            const auto prefix = std::string(c.word) + bracket;
            HD_CHECK_EQ(hdTrans::output::ApplyJosaAlternation(prefix + c.pattern, {}, false), prefix + c.form);
            HD_CHECK_EQ(hdTrans::output::ApplyJosaAlternation(c.pattern, prefix, true), std::string(c.form));
        }
    }
}
