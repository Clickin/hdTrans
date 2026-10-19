#include <chrono>
#include <vector>

#include "hdTrans/output/compose.h"
#include "hdTrans/output/text.h"
#include "hdTrans/tokenizer/token.h"
#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::output;

HD_TEST(compose_sentence_token_ranges) {
    std::vector<TokenText> tokens(12000);
    for (TokenText& token : tokens) {
        token.type = tokenizer::kTypeAlpha;
        token.text = "text";
    }
    Composer composer;
    Families families;
    Settings settings;
    settings.text = true;
    std::vector<ResolvedItem> items;
    Pieces result;
    const auto start = std::chrono::steady_clock::now();
    for (int first = 0; first < static_cast<int>(tokens.size()); first += 3) {
        Pieces sentence;
        composer.Compose(items, tokens, first, first + 2, {}, settings, families, sentence);
        HD_CHECK_EQ(String(sentence), "texttexttext");
        Append(result, sentence);
    }
    HD_CHECK_EQ(String(result).size(), std::size_t{48000});
    for (const TokenText& token : tokens) HD_CHECK_EQ(token.text, "text");
    std::printf("compose 4000 sentences: %.3f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}
