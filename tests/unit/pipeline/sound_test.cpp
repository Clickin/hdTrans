#include "hdTrans/pipeline/sound.h"

#include <cstdlib>
#include <memory>
#include <string>

#include "hdTrans/dict/snapshot.h"
#include "hdtest.h"

using namespace hdTrans;
using namespace hdTrans::pipeline;

namespace {

std::string DictsDir() {
    if (const char* d = std::getenv("HDTRANS_DICTS"); d != nullptr && *d != 0) return d;
    std::string f = __FILE__;
    const size_t slash = f.find_last_of("/\\");
    return (slash == std::string::npos ? std::string(".") : f.substr(0, slash)) + "/../../../../dicts/base";
}

// The snapshot of the dictionary folder, null when it cannot be loaded.
std::shared_ptr<const dict::Snapshot> Dicts() {
    static const std::shared_ptr<const dict::Snapshot> snap = dict::LoadFolders({DictsDir()}).snapshot;
    return snap;
}

struct Case {
    const char* in;
    const char* want;
};

}  // namespace

HD_TEST(pipeline_lengthen) {
    const struct {
        const char* korean;
        const char* units;
        const char* want;
    } cases[] = {
        {"하아", "vv", "하아아아"}, {"앙", "vvv", "아아아앙"}, {"꺅", "vv", "꺄아악"},
        {"앙", "~", "아~앙"},       {"하아", "!", "하앗"},     {"하아", "~", "하아~"},
        {"정말", "vv", "정말~"},    {"쾅", "v", "콰앙"},       {"하아", "vvvvvvv", "하아아아아아"},
        {"정말", "!", "정말"},      {"ok", "vv", "ok"},        {"", "v", ""},
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::Lengthen(c.korean, c.units), std::string(c.want));
}

HD_TEST(pipeline_sound_lexicon) {
    HD_CHECK(internal::IsSoundKey(U"ドキドキ"));
    HD_CHECK(!internal::IsSoundKey(U"ドキ"));
    HD_CHECK(!internal::IsSoundKey(U""));
    for (const internal::SoundRow& r : internal::SoundRows(U'ド')) HD_CHECK(!r.key.empty() && r.key[0] == U'ド');
    size_t prev = static_cast<size_t>(-1);
    for (const internal::SoundRow& r : internal::SoundRows(U'ア')) {
        HD_CHECK(r.key.size() <= prev);
        prev = r.key.size();
    }
    // Invalid bytes survive the character view.
    HD_CHECK_EQ(internal::EncodeText(internal::DecodeText("の\xff\xe3\x81")), std::string("の\xff\xe3\x81"));
}

// The sound stage of whole lines: sounds, elongation, stutter (U+E000 before each
// separator), grammar after a sound, words that are not sounds, the emphasis and casual
// adjective rewrites and the lead interjections.
HD_TEST(pipeline_sound) {
    const auto snap = Dicts();
    if (snap == nullptr) {
        std::printf("skip pipeline_sound: no dictionaries at %s\n", DictsDir().c_str());
        return;
    }
    LineContext ctx;
    ctx.lookup.plain = &snap->TextPlain();
    const Case cases[] = {
        {"あんっ、だめぇ", "앙、안 돼"},
        {"うわ…柔らかい", "우와…柔らかい"},
        {"フキフキ", "쓱쓱"},
        {"肉がジュージューと焼ける。", "肉が지글지글 焼ける。"},
        {"ああああん", "아아아앙"},
        {"はぁぁぁ…", "하아아아…"},
        {"いやぁぁぁ！", "싫어어어！"},
        {"うわぁー！", "우와아아！"},
        {"きゃああっ！", "꺄아악！"},
        {"やだ～～！", "싫어~！"},
        {"いくぅぅ", "가아아"},
        {"な、なにを", "\xEE\x80\x80" "、なにを"},
        {"だっ、だめ", "\xEE\x80\x80" "、だめ"},
        {"お…お兄ちゃん", "\xEE\x80\x80" "…お兄ちゃん"},
        {"「わ、わ、私は……」", "「\xEE\x80\x80" "、\xEE\x80\x80" "、私は……」"},
        {"ドキドキしてきた", "두근두근してきた"},
        {"ぼーっとしてない？", "멍してない？"},
        {"くすくす笑う", "くすくす笑う"},
        {"シーンを撮る", "シーンを撮る"},
        {"パンを食べた", "パンを食べた"},
        {"あんな人", "あんな人"},
        {"まっかせてよ！", "まかせてよ！"},
        {"やーめーてよ！", "やめてよ！"},
        {"見ぃつけたぁ！", "見つけたぁ！"},
        {"うるさ～い！", "うるさい！"},
        {"ちょーっと待って", "ちょっと待って"},
        {"どーだ！", "どうだ！"},
        {"いこー！", "いこう！"},
        {"なんだそりゃああぁあッ？", "なんだそりゃッ？"},
        {"もっと早く", "もっと早く"},
        {"鍵が……おーいだれか", "鍵が……おーいだれか"},
        {"うーちゃん！", "うーちゃん！"},
        {"休もうっと。", "休もうっと。"},
        {"うるせぇな", "うるさいな"},
        {"すげーな", "すごいな"},
        {"冷て～！", "冷たい！"},
        {"歌おうぜぇ～。", "歌おうぜぇ～。"},
        {"届いてぇ！", "届いてぇ！"},
        {"「おい、誰か！！」", "「어이、誰か！！」"},
        {"「お、おい、魅…、」", "「\xEE\x80\x80" "、어이、魅…、」"},
        {"おいおいおい", "이봐 이봐 이봐"},
        {"ほら、見て！", "자、見て！"},
        {"ちょっと高い", "ちょっと高い"},
        {"これでよし。", "これでよし。"},
        {"<b>きゃあ</b>ぎゅっと", "<b>꺄아</b>꽉"},
        // Invalid UTF-8 bytes stay in place.
        {"の\xff<b>", "の\xff<b>"},
        {"\xffドキドキ\xe3", "\xff두근두근\xe3"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(Sound(c.in, ctx, *snap), std::string(c.want));
}

HD_TEST(pipeline_sound_markers) {
    const auto snap = Dicts();
    if (snap == nullptr) return;
    const std::string markers[] = {"ドキ"};
    LineContext ctx;
    ctx.lookup.plain = &snap->TextPlain();
    ctx.markers = markers;
    HD_CHECK_EQ(internal::WriteSounds("ドキドキ", ctx, *snap), std::string("ドキドキ"));
}

HD_TEST(pipeline_rewrite_emphasis) {
    const auto snap = Dicts();
    if (snap == nullptr) return;
    LineContext ctx;
    ctx.lookup.plain = &snap->TextPlain();
    const Case cases[] = {
        {"まっかせてよ！", "まかせてよ！"},
        {"やーめーてよ！", "やめてよ！"},
        {"見ぃつけたぁ！", "見つけたぁ！"},
        {"うるさ～い！", "うるさい！"},
        {"ちょーっと待って", "ちょっと待って"},
        {"どーだ！", "どうだ！"},
        {"いこー！", "いこう！"},
        {"なんだそりゃああぁあッ？", "なんだそりゃッ？"},
        // Unchanged: a word holds the mark, a sound, a nickname, an ending.
        {"もっと早く", "もっと早く"},
        {"鍵が……おーいだれか", "鍵が……おーいだれか"},
        {"うーちゃん！", "うーちゃん！"},
        {"休もうっと。", "休もうっと。"},
        {"なんとかなるっしょ。", "なんとかなるっしょ。"},
        {"日曜日じゃなかったっけ", "日曜日じゃなかったっけ"},
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::RewriteEmphasis(c.in, ctx, *snap), std::string(c.want));
}

HD_TEST(pipeline_rewrite_casual_adjectives) {
    const auto snap = Dicts();
    if (snap == nullptr) return;
    LineContext ctx;
    ctx.lookup.plain = &snap->TextPlain();
    const Case cases[] = {
        {"うるせぇな", "うるさいな"},
        {"すげーな", "すごいな"},
        {"冷て～！", "冷たい！"},
        {"歌おうぜぇ～。", "歌おうぜぇ～。"},  // the particle ぜ
        {"届いてぇ！", "届いてぇ！"},          // a て form
    };
    for (const auto& c : cases) HD_CHECK_EQ(internal::RewriteCasualAdjectives(c.in, ctx, *snap), std::string(c.want));
}

HD_TEST(pipeline_sound_extra_isolation) {
    HD_CHECK(!internal::IsSoundKey(U"ガボッ"));
    HD_CHECK(internal::IsSoundKey(U"ガボッ", true));
    HD_CHECK(!internal::IsSoundKey(U"ガボッ"));
}
