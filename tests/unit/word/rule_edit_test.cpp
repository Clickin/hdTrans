// Editing one line of the compiled-in rules program changes the selection the word stage makes.
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/rules.h"
#include "core/tokenizer/tokenize.h"
#include "core/word/analyze.h"
#include "core/word/flush.h"
#include "core/word/worker.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

// The text of src/core/rules/rules.txt with the record of local rule index replaced by line;
// empty when there is no such record.
std::string EditedRules(int index, std::string_view line) {
    std::string text;
    for (std::string_view l : internal::RuleLines()) text += l;
    const std::string prefix = "\nL " + std::to_string(index) + " ";
    const std::size_t start = text.find(prefix);
    if (start == std::string::npos) return {};
    const std::size_t end = text.find('\n', start + 1);
    text.replace(start + 1, end - start - 1, line);
    return text;
}

// The target selected for 人気 in 人気。 under program, with the two rows below.
std::string Selected(const rules::Program& program, word::Selection& choice) {
    std::vector<dict::File> files{{0, "base.tsv",
        "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
        "人気\t인적\tN110\tXXX\t\tGN\t0\t1\n"
        "人気\t인기\tN110\t\t\tGN\t2\t2\n"}};
    const dict::LoadResult loaded = dict::Load(std::move(files));
    if (!loaded.errors.empty() || loaded.snapshot == nullptr) return "(no dictionary)";
    tokenizer::Tokens tokens;
    word::Arena arena;
    word::Analysis analysis;
    tokenizer::Tokenize("人気。", tokens);
    word::Analyze(tokens, *loaded.snapshot, {}, word::Settings::Reference(), arena, analysis);
    if (analysis.fault || analysis.sentences.empty()) return "(no analysis)";
    word::Worker& w = *analysis.worker;
    w.chain = analysis.sentences[0].chain;
    w.current = analysis.sentences[0].current;
    w.transformSurface = analysis.sentences[0].transformSurface;
    word::Flusher f{w, program, loaded.snapshot->OutputFamilies()};
    word::Flush(f);
    for (word::Item* it = w.chain.head; it != nullptr; it = it->next) {
        if (it->group->surface != "人気") continue;
        choice = it->choice;
        return std::string(it->EntryText(it->selected));
    }
    return "(no item)";
}

}  // namespace

HD_TEST(rules_edit_changes_selection) {
    struct Case {
        const char* line;  // the new record of local rule 2; null for the shipped one
        const char* target;
        word::Provenance layer;
    };
    const Case cases[] = {
        {nullptr, "인기", word::Provenance::LocalRule},
        {"L 2 ||0", "인적", word::Provenance::FirstXxx},
        {"L 2 ||1 S+0== \"人\"", "인적", word::Provenance::FirstXxx},
        {"L 2 ||1 S+0== \"L人\"", "인기", word::Provenance::LocalRule},
        {"L 2 ||1 &&2 S+0== \"L人\" S-1== \"の\"", "인적", word::Provenance::FirstXxx},
        {"L 2 ||1 D+0= \"인기도\"", "인기도", word::Provenance::LocalRule},
    };
    for (const Case& c : cases) {
        rules::Program program = internal::RulesProgram();
        if (c.line) {
            rules::LoadError error{};
            HD_REQUIRE(rules::Load(EditedRules(2, c.line), program, error));
            HD_CHECK_EQ(program.localRules.size(), internal::RulesProgram().localRules.size());
        }
        word::Selection choice;
        HD_CHECK_EQ(Selected(program, choice), std::string(c.target));
        HD_CHECK_EQ(static_cast<int>(choice.layer), static_cast<int>(c.layer));
    }
}

// A broken record makes the whole program fail to load, with its line.
HD_TEST(rules_edit_bad_line) {
    rules::Program program;
    rules::LoadError error{};
    HD_CHECK(!rules::Load(EditedRules(2, "L 2 ||1 S+1==  \"が\""), program, error));
    HD_CHECK(error.line > 0);
    HD_CHECK(!rules::Load(EditedRules(2, "L 2 &&1 S+1== \"が\""), program, error));
}
