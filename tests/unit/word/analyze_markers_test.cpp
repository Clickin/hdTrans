// Control markers and the full-width space after a "Wa" word, typed in: the analyze
// records (CM, L and LX) of short inputs over a small in-memory dictionary, and the
// overlay of the space token. The expectations are the records the word stage writes for
// these inputs; the LX digests are of the empty plain-entry lists of this dictionary.
#include <string>
#include <vector>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/tokenizer/tokenize.h"
#include "hdTrans/word/analyze.h"
#include "hdTrans/word/worker.h"
#include "hdtest.h"
#include "word/analyze_records.h"

using namespace hdTrans;

namespace {

// A base dictionary with one word that carries the grammar code Wa (鶏魚 before a
// full-width space).
std::shared_ptr<const dict::Snapshot> SmallDictionary() {
    dict::File base;
    base.name = "base.tsv";
    base.text = "key\tko\tpos\tsem\tgram\tdict\tid\n鶏魚\t이사키\tN130\tQPN\tWa\tGN\t1\n";
    return dict::Load({base}).snapshot;
}

// The CM, L and LX records of the sentences of one input line (raw path: one piece).
std::vector<std::string> MarkerRecords(const std::string& text, tokenizer::Tokens& tokens,
                                       word::Analysis& analysis) {
    std::vector<tokenizer::Piece> pieces{{text, 0}};
    tokenizer::TokenizeSpans(pieces, false, tokens);
    word::Arena arena;
    const auto snapshot = SmallDictionary();
    word::Analyze(tokens, *snapshot, dict::LookupContext{}, word::Settings::Reference(), arena, analysis);
    std::vector<std::string> out;
    for (std::size_t s = 0; s < analysis.sentences.size(); s++) {
        std::vector<std::string> all;
        analyzetest::SentenceRecords(tokens, analysis.sentences[s], 0, static_cast<int>(s), all);
        for (std::string& line : all) {
            const std::string_view tag = analyzetest::Tag(line);
            if (tag == "CM" || tag == "L" || tag == "LX") out.push_back(std::move(line));
        }
    }
    return out;
}

}  // namespace

HD_TEST(analyze_control_markers_records) {
    struct Case {
        std::string text;
        std::vector<std::string> want;
    };
    const std::vector<Case> cases = {
        {"あ&#CSい。", {
            "CM\t0\t0\t0\t3\t1",
            "L\t0\t0\t0:0\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:3\t3\tい\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\td6474a4d77b7dcc3\t0\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:3\td6474a4d77b7dcc3\t3\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"あ &#CS い。", {
            "CM\t0\t0\t0\t3\t1",
            "L\t0\t0\t0:0\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:3\t3\tい\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\td6474a4d77b7dcc3\t0\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:3\td6474a4d77b7dcc3\t3\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"<a>あ</a>い。", {
            "CM\t0\t0\t1\t3\t2",
            "L\t0\t0\t1:0\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t1:3\t3\tい\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t1:0\td6474a4d77b7dcc3\t0\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t1:3\td6474a4d77b7dcc3\t3\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚　あ。", {
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:6\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t17427f35df9bd3a5\t0\t6\t6\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:6\t17427f35df9bd3a5\t6\t3\t3\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚　行く。", {
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:6\t3\t行\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:9\t3\tく\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\tdd6e96afd193d03f\t0\t6\t6\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:6\tdd6e96afd193d03f\t6\t3\t3\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:9\tdd6e96afd193d03f\t9\t3\t3\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚 あ。", {
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:6\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t17427f35df9bd3a5\t0\t6\t6\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:6\t17427f35df9bd3a5\t6\t3\t3\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"少水　鶏魚。", {
            "L\t0\t0\t0:0\t3\t少\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:3\t3\t水\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:6\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\tf494470a87d7d9bb\t0\t3\t3\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:3\tf494470a87d7d9bb\t3\t3\t3\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:6\tf494470a87d7d9bb\t6\t6\t6\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"<BR>あ<BR>い。", {
            "L\t0\t1\t1:0\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t1\t1:0\t4d99551b8369fad1\t0\t3\t3\t77\t<BR>\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "L\t0\t2\t3:0\t3\tい\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t2\t3:0\t4d99571b8369fe37\t0\t3\t3\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚&#CS　あ。", {
            "CM\t0\t0\t0\t6\t1",
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:6\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t17427f35df9bd3a5\t0\t6\t6\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:6\t17427f35df9bd3a5\t6\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚　&#CS あ。", {
            "CM\t0\t0\t0\t6\t1",
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:6\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t17427f35df9bd3a5\t0\t6\t6\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:6\t17427f35df9bd3a5\t6\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚&#CS　行く。", {
            "CM\t0\t0\t0\t6\t1",
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:6\t3\t行\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:9\t3\tく\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\tdd6e96afd193d03f\t0\t6\t6\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:6\tdd6e96afd193d03f\t6\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:9\tdd6e96afd193d03f\t9\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚&#CS 行く。", {
            "CM\t0\t0\t0\t6\t1",
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:6\t3\t行\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:9\t3\tく\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\tdd6e96afd193d03f\t0\t6\t6\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:6\tdd6e96afd193d03f\t6\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:9\tdd6e96afd193d03f\t9\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"あ　&#CS　い。", {
            "CM\t0\t0\t0\t3\t1",
            "L\t0\t0\t0:0\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:3\t3\tい\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\td6474a4d77b7dcc3\t0\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:3\td6474a4d77b7dcc3\t3\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚&#CS　。", {
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t2e4730e645819ea1\t0\t6\t6\t77\t&#CS\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚&#CS　&#CS。", {
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t2e4730e645819ea1\t0\t6\t6\t77\t&#CS\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚&#CS　、あ。", {
            "CM\t0\t0\t0\t6\t1",
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:6\t3\t、\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:9\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\td1cfe4e3de8af49d\t0\t6\t6\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:6\td1cfe4e3de8af49d\t6\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:9\td1cfe4e3de8af49d\t9\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚&#CS　<BR>あ。", {
            "CM\t0\t0\t0\t6\t1",
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:6\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t17427f35df9bd3a5\t0\t6\t6\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:6\t17427f35df9bd3a5\t6\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚&#CS　あ<BR>。", {
            "CM\t0\t0\t0\t6\t1",
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:6\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t17427f35df9bd3a5\t0\t6\t6\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:6\t17427f35df9bd3a5\t6\t3\t3\t77\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚　。", {
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t2e4730e645819ea1\t0\t6\t6\t26\t　\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚　「あ」。", {
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t2:0\t3\t「\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t2:3\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t2:6\t3\t」\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t2e4730e645819ea1\t0\t6\t6\t26\t　\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t2:0\t0f27e9a19b5b39e2\t0\t3\t3\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t2:3\t0f27e9a19b5b39e2\t3\t3\t3\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t2:6\t0f27e9a19b5b39e2\t6\t3\t3\t17\t。\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚　(あ)", {
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t2:0\t1\t(\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t2:1\t3\tあ\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t2:4\t1\t)\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t2e4730e645819ea1\t0\t6\t6\t26\t　\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t2:0\t85558d0d6471eae2\t0\t1\t1\t9\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t2:1\t85558d0d6471eae2\t1\t3\t3\t9\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t2:4\t85558d0d6471eae2\t4\t1\t1\t9\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚　x", {
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t2:0\t1\tx\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t2e4730e645819ea1\t0\t6\t6\t26\t　\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t2:0\taf63f54c86021707\t0\t1\t1\t9\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"鶏魚　ＡＢ", {
            "L\t0\t0\t0:0\t6\t鶏魚\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t2:0\t2\tAB\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\t2e4730e645819ea1\t0\t6\t6\t26\t　\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t2:0\t09086407b5a0edaa\t0\t2\t2\t9\t\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
        {"少水　。", {
            "L\t0\t0\t0:0\t3\t少\t1\tTWordInfo\t-1\t\t0",
            "L\t0\t0\t0:3\t3\t水\t1\tTWordInfo\t-1\t\t0",
            "LX\t0\t0\t0:0\tea291e5ee1a930fb\t0\t3\t3\t26\t　\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
            "LX\t0\t0\t0:3\tea291e5ee1a930fb\t3\t3\t3\t26\t　\t-1\t0\t0\t0\t-1\t0\t-1\t0\t0\t\t0\tcbf29ce484222325\t0\tcbf29ce484222325\t0\t07cfe807b49795b2",
        }},
    };
    for (const Case& c : cases) {
        tokenizer::Tokens tokens;
        word::Analysis analysis;
        const std::vector<std::string> got = MarkerRecords(c.text, tokens, analysis);
        HD_CHECK_EQ(got.size(), c.want.size());
        for (std::size_t i = 0; i < got.size() && i < c.want.size(); i++) {
            if (got[i] != c.want[i]) {
                hdtest::Fail(__FILE__, __LINE__, c.text + " record " + std::to_string(i) + "\n  got  " + got[i] +
                                                     "\n  want " + c.want[i]);
            }
        }
    }
}

// A full-width space token that follows a word with the grammar code Wa is never visited:
// the overlay reads it as a wide space (type 26, text " "), and it is the only spaced token.
// The overlay needs the Wa word to be the last item of its token; a word merged with the
// following text does not qualify, and an ASCII space is not overlaid.
HD_TEST(analyze_wa_space_overlay) {
    struct Case {
        std::string text;
        int spaced;
    };
    const Case cases[] = {
        {"鶏魚　。", 1},        {"鶏魚　「あ」。", 1}, {"鶏魚　(あ)", 1},    {"鶏魚　x", 1},
        {"鶏魚　ＡＢ", 1},      {"少水　。", 0},      {"鶏魚&#CS　。", 0}, {"鶏魚&#CS　&#CS。", 0},
        {"鶏魚&#CS　、あ。", 0}, {"鶏魚&#CS　<BR>あ。", 0}, {"鶏魚&#CS　あ。", 0}, {"鶏魚&#CS　行く。", 0},
        {"鶏魚&#CS 行く。", 0}, {"あ　&#CS　い。", 0}, {"鶏魚　あ。", 0},
    };
    for (const Case& c : cases) {
        tokenizer::Tokens tokens;
        word::Analysis analysis;
        MarkerRecords(c.text, tokens, analysis);
        int nonEmpty = 0;
        for (const word::Sentence& s : analysis.sentences) nonEmpty += s.chain.head != nullptr ? 1 : 0;
        HD_CHECK_EQ(nonEmpty, 1);
        int spaced = 0;
        for (std::size_t j = 0; j < analysis.spaced.size(); j++) {
            if (analysis.spaced[j] == 0) continue;
            spaced++;
            const tokenizer::TokenView view = analysis.worker->Tok(static_cast<int>(j));
            HD_CHECK_EQ(view.type, tokenizer::kTypeWideSpace);
            HD_CHECK_EQ(std::string(view.text), std::string(" "));
            HD_CHECK_EQ(std::string(analysis.worker->Typed(static_cast<int>(j))), std::string(" "));
        }
        HD_CHECK_EQ(spaced, c.spaced);
    }
}

HD_TEST(word_improvements_reference) {
    const word::Settings settings = word::Settings::Reference();
    HD_CHECK(!settings.improvements.documentSenses);
    HD_CHECK(!settings.improvements.desireWindow);
    HD_CHECK(!settings.improvements.expandedGroups);
    HD_CHECK(!settings.improvements.semanticAlternatives);
    HD_CHECK(!settings.improvements.preserveNeighbourSlots);
    HD_CHECK(!settings.improvements.perLevelRetries);
    HD_CHECK(!settings.improvements.nonterminalBoundary);
    HD_CHECK(!settings.improvements.tailLengthCheck);
    HD_CHECK(!settings.improvements.preparedNextToken);
    HD_CHECK(settings.sessionSenses == nullptr);
    HD_CHECK(word::Settings{}.improvements.documentSenses);
    HD_CHECK(word::Settings{}.improvements.desireWindow);
}

HD_TEST(word_prepared_next_token) {
    const auto snapshot = SmallDictionary();
    HD_REQUIRE(snapshot != nullptr);
    tokenizer::Tokens tokens;
    tokenizer::Tokenize("作るってー？", tokens);
    HD_REQUIRE(tokens.size() >= 3);
    for (bool prepared : {false, true}) {
        word::Arena arena;
        word::Settings settings = word::Settings::Reference();
        settings.improvements.preparedNextToken = prepared;
        word::Worker worker(tokens, *snapshot, {}, settings, arena, {});
        worker.current = 0;
        HD_CHECK_EQ(std::string(worker.NextText(1)), prepared ? "―？" : "ー？");
        const auto* detail = worker.NewDetail(word::kDetailInfo, 0);
        HD_CHECK_EQ(std::string(detail->nextText), prepared ? "―？" : "ー？");
        worker.current = 1;
        HD_CHECK_EQ(std::string(worker.NextText(1)), "―？");
    }
}
