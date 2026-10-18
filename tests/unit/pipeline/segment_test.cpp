#include "hdTrans/pipeline/segment.h"

#include <string>
#include <vector>

#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::pipeline;

namespace {

// The chunks joined by |, spans written as 〖kind text〗.
std::string Show(const std::vector<Chunk>& chunks) {
    std::string s;
    for (size_t i = 0; i < chunks.size(); i++) {
        if (i > 0) s += "|";
        for (const output::Piece& p : chunks[i]) {
            s += p.span == output::SpanText ? p.text : "〖" + std::to_string(static_cast<int>(p.span)) + p.text + "〗";
        }
    }
    return s;
}

std::string Cut(const char* text) { return Show(Segment({{text, output::SpanText}})); }

}  // namespace

HD_TEST(pipeline_segment_spaces) {
    const struct {
        const char* in;
        const char* want;
    } cases[] = {
        {"第一章　完", "第一章|完"},
        {"えっ！？　本当に？", "えっ！？|本当に？"},
        {"第一章１　　『ギザ十』", "第一章１|『ギザ十』"},
        {"　段落の始まり。", "　段落の始まり。"},
        {"こ　の　展　開", "こ　の　展　開"},
        {"猫　犬", "猫　犬"},
        {"第一章 完", "第一章 完"},
        {"終わり　\n次", "終わり　\n次"},
        // An ASCII space after a final particle or between hiragana and a kanji word.
        {"そうだね そうじゃない", "そうだね|そうじゃない"},
        {"ひろい集め 彼は", "ひろい集め|彼は"},
        {"ほら 上も", "ほら|上も"},
        {"おなかが すきました", "おなかが すきました"},
        {"美しい 花", "美しい 花"},
        {"大見 健", "大見 健"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(Cut(c.in), std::string(c.want));
}

HD_TEST(pipeline_segment_clauses) {
    // At least 40 Japanese characters before the 、 and 10 characters after it before
    // the sentence ends.
    HD_CHECK_EQ(Cut("覚えかけの必殺技の修得率を知りたい場合は、メニュー画面からステータス表示画面へ飛んで、"
                    "修得技を見れば一発でわかるぞ。"),
                std::string("覚えかけの必殺技の修得率を知りたい場合は、メニュー画面からステータス表示画面へ飛んで、|"
                            "修得技を見れば一発でわかるぞ。"));
    HD_CHECK_EQ(Segment({{"兄ちゃんが兵隊に行ってから、新しくウェイトレスが入ったんだけど……ぼく、兄ちゃんが帰ったら、"
                          "またすぐにレストランで働けるように頑張ってるんだ。",
                          output::SpanText}})
                    .size(),
                size_t{2});
    HD_CHECK_EQ(Segment({{"男に生まれて、世界中を旅してまわって、いろんなことをして生きてみたかったなあ。", output::SpanText}})
                    .size(),
                size_t{1});
    // Too little after the 、 before the sentence ends.
    HD_CHECK_EQ(Cut("あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわて、短い。次の文"),
                std::string("あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわて、短い。次の文"));
    // A sentence end restarts the count; spans do not count before the 、 but count
    // after it.
    const std::string a30 = "あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほ";
    const std::string c38 = a30 + "まみむめもやゆよ";
    const std::string rest = "て、それから次へと進むのだ";
    HD_CHECK_EQ(Cut((c38 + "ま" + rest).c_str()), c38 + "まて、|それから次へと進むのだ");
    HD_CHECK_EQ(Cut((c38 + rest).c_str()), c38 + rest);
    HD_CHECK_EQ(Cut((a30 + "。" + a30 + rest).c_str()), a30 + "。" + a30 + rest);
    const output::Pieces before = {{"<b>", output::SpanOpen}, {c38 + rest, output::SpanText}};
    HD_CHECK_EQ(Segment(before).size(), size_t{1});
    const output::Pieces after = {{c38 + "まて、それ", output::SpanText},
                                  {"김", output::SpanWord},
                                  {"김", output::SpanWord},
                                  {"から次へと進む", output::SpanText}};
    HD_CHECK_EQ(Show(Segment(after)), c38 + "まて、|それ〖1김〗〖1김〗から次へと進む");
    HD_CHECK_EQ(Cut((c38 + "まて、それから次へと進む").c_str()), c38 + "まて、それから次へと進む");
    // The white space after a cut goes; a chunk of white space only goes whole.
    HD_CHECK_EQ(Show(Segment({{"第一章　", output::SpanText}, {"김", output::SpanWord}})), std::string("第一章　〖1김〗"));
    HD_CHECK_EQ(Show(Segment({{"第一章　 \t完", output::SpanText}})), std::string("第一章|完"));
    HD_CHECK(Segment({}).size() == 1 && Segment({})[0].empty());
}

HD_TEST(pipeline_segment_rules) {
    const struct {
        const char* before;
        bool want;
    } conjunctive[] = {
        {"飛んで", true}, {"学校で", false}, {"言って", true}, {"帰ったら", true}, {"知らないけど", true},
        {"思ったが", true}, {"ですが", true}, {"彼女が", false}, {"いいし", true}, {"わたし", false},
        {"はい", false}, {"行くので", true}, {"しながら", true}, {"で", false}, {"", false},
    };
    for (const auto& c : conjunctive) HD_CHECK_EQ(internal::Conjunctive(c.before), c.want);

    HD_CHECK(internal::KanaSpace("すきだよ すきだよ", std::string("すきだよ").size()));
    HD_CHECK(!internal::KanaSpace("かがやく おでこ", std::string("かがやく").size()));
    HD_CHECK(!internal::KanaSpace("基本操作 について", std::string("基本操作").size()));
    HD_CHECK(!internal::KanaSpace("だよ  ", std::string("だよ").size()));

    HD_CHECK(internal::BetweenWords("第一章　完", std::string("第一章").size()));
    HD_CHECK(!internal::BetweenWords("猫　犬", std::string("猫").size()));
    HD_CHECK(!internal::BetweenWords("こ　の　展", std::string("こ").size()));
    HD_CHECK(!internal::BetweenWords("　段落", 0));
    HD_CHECK(!internal::BetweenWords("終わり　\n次", std::string("終わり").size()));
}
