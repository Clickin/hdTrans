#include <initializer_list>
#include <string>
#include <string_view>

#include "hdTrans/word/flush.h"
#include "hdTrans/word/document_senses.h"
#include "hdTrans/word/select.h"
#include "hdtest.h"

namespace {

struct Candidate {
    std::string_view target;
    std::string_view punctuation;
};

struct SelectionLine {
    hdTrans::tokenizer::Tokens tokens;
    hdTrans::word::Arena arena;
    hdTrans::word::Worker worker;
    hdTrans::rules::Program program;
    hdTrans::word::Flusher flusher;
    hdTrans::word::Item* item;

    SelectionLine(const hdTrans::dict::Snapshot& snapshot, const hdTrans::word::Settings& settings,
                  std::string_view text, std::initializer_list<Candidate> candidates)
        : worker(tokens, snapshot, {}, settings, arena, {}),
          flusher{worker, program, snapshot.OutputFamilies()} {
        auto* group = worker.NewGroup(nullptr, '0');
        group->surface = "人気";
        group->length = static_cast<int>(group->surface.size());
        for (const auto& candidate : candidates) {
            hdTrans::word::Record record;
            record.target = candidate.target;
            record.partCode = "N110";
            record.attr = candidate.punctuation;
            record.ruleNo = -1;
            group->entries.push_back(worker.NewEntry(record));
        }
        item = worker.NewItem(worker.NewDetail(hdTrans::word::kDetailInfo, 0), group, text, 0, 0);
        worker.chain.head = worker.chain.tail = item;
    }
};

int Popularity(const hdTrans::word::Item& item, int candidate, const hdTrans::word::SelectContext&) {
    return item.EntryText(candidate) == "인기" ? 10 : 0;
}

}  // namespace

HD_TEST(word_shadow_scorers_leave_active_scorers_effective) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv", "key\tko\tpos\tid\n"}});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const word::Scorer active[] = {{"active-popularity", Popularity}};
    const word::Scorer diagnostics[] = {{"shadow-shortest", word::ShortestTargetScore}};
    for (const bool shadow : {false, true}) {
        auto settings = word::Settings::Reference();
        settings.scorers = active;
        settings.shadowScorers = diagnostics;
        settings.selectShadow = shadow;
        SelectionLine line(*loaded.snapshot, settings, "人気", {{"유명세", ""}, {"인기", ""}, {"짧", ""}});
        line.item->SelectCandidate(0, {word::Provenance::FirstXxx});
        word::Select(line.flusher);
        HD_CHECK_EQ(line.item->selected, 1);
        HD_CHECK(line.item->choice.layer == word::Provenance::Scorer);
        HD_CHECK_EQ(line.item->choice.id, 0);
        if (shadow) {
            HD_REQUIRE(line.item->shadowChoices.size() == 1);
            const auto& choice = line.item->shadowChoices[0];
            HD_CHECK_EQ(std::string(choice.scorer), std::string("shadow-shortest"));
            HD_CHECK_EQ(choice.from, 0);
            HD_CHECK_EQ(choice.candidate, 2);
            HD_CHECK_EQ(choice.target, std::string("짧"));
            HD_CHECK(choice.eligible && choice.winner);
        } else HD_CHECK(line.item->shadowChoices.empty());
    }
}

HD_TEST(word_select_preserves_punctuation_choice_without_rules) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv", "key\tko\tpos\tid\n"}});
    HD_REQUIRE(loaded.snapshot != nullptr);
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}};
    for (const bool rules : {false, true}) {
        for (const char punctuation : {',', '.'}) {
            auto settings = word::Settings::Reference();
            settings.scorers = scorers;
            settings.shadowScorers = scorers;
            settings.selectShadow = true;
            settings.selectRules = rules;
            const std::string input = std::string("人気") + punctuation;
            SelectionLine line(*loaded.snapshot, settings, input, {{"짧", "J1"}, {"긴문장", "J2"}});
            word::CleanupSelectors(line.flusher);
            const int contextual = punctuation == '.' ? 1 : 0;
            HD_REQUIRE(line.item->selected == contextual);
            HD_REQUIRE(line.item->choice.layer == word::Provenance::Digit);
            HD_CHECK_EQ(line.item->CheckJ(contextual), 1);
            HD_CHECK_EQ(line.item->CheckJ(1 - contextual), -1);
            word::Select(line.flusher);
            const bool changed = rules && contextual == 1;
            HD_CHECK_EQ(line.item->selected, changed ? 0 : contextual);
            HD_CHECK(line.item->choice.layer == (changed ? word::Provenance::Scorer : word::Provenance::Digit));
            HD_REQUIRE(line.item->shadowChoices.size() == 1);
            const auto& choice = line.item->shadowChoices[0];
            HD_CHECK_EQ(choice.from, contextual);
            HD_CHECK_EQ(choice.candidate, 0);
            HD_CHECK_EQ(choice.eligible, rules);
            HD_CHECK_EQ(choice.winner, changed);
        }
    }
}

HD_TEST(document_senses_preserve_punctuation_choice_even_with_rules) {
    using namespace hdTrans;
    const auto loaded = dict::Load({{0, "base.tsv", "key\tko\tpos\tid\n"}});
    HD_REQUIRE(loaded.snapshot != nullptr);
    for (const bool shadow : {false, true}) {
        word::SessionSenses senses;
        senses.choices.emplace("人気", word::SessionSense{"짧", "N110"});
        auto settings = word::Settings::Reference();
        settings.improvements.documentSenses = true;
        settings.sessionSenses = &senses;
        settings.selectRules = true;
        settings.selectShadow = shadow;
        SelectionLine line(*loaded.snapshot, settings, "人気.", {{"짧", "J1"}, {"긴문장", "J2"}});
        word::CleanupSelectors(line.flusher);
        HD_REQUIRE(line.item->selected == 1);
        HD_REQUIRE(line.item->choice.layer == word::Provenance::Digit);
        const word::SelectContext context{*loaded.snapshot, line.worker.chain};
        HD_CHECK_EQ(word::DocumentSenseScore(*line.item, 0, context), 1);
        HD_CHECK_EQ(word::DocumentSenseScore(*line.item, 1, context), 0);
        word::Select(line.flusher);
        HD_CHECK_EQ(line.item->selected, 1);
        HD_CHECK(line.item->choice.layer == word::Provenance::Digit);
        if (shadow) {
            HD_REQUIRE(line.item->shadowChoices.size() == 1);
            const auto& choice = line.item->shadowChoices[0];
            HD_CHECK_EQ(choice.candidate, 0);
            HD_CHECK(!choice.eligible && !choice.winner);
        } else HD_CHECK(line.item->shadowChoices.empty());
    }
}
