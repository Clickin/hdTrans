#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/word/flush.h"
#include "hdTrans/word/select.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

std::shared_ptr<const dict::Snapshot> Dictionary() {
    const auto loaded = dict::Load({{0, "base.tsv", "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"}});
    HD_CHECK(loaded.errors.empty());
    return loaded.snapshot;
}

struct Candidate {
    std::string_view target;
    std::string_view part = "N110";
};

struct SelectLine {
    tokenizer::Tokens tokens;
    word::Arena arena;
    word::Worker worker;
    rules::Program program;
    word::Flusher flusher;

    explicit SelectLine(const dict::Snapshot& snapshot, word::Settings settings = word::Settings::Reference())
        : worker(tokens, snapshot, {}, settings, arena, {}),
          flusher{worker, program, snapshot.OutputFamilies()} {}

    word::Group* MakeGroup(std::string_view surface, std::initializer_list<Candidate> candidates) {
        word::Group* group = worker.NewGroup(nullptr, '0');
        group->surface = surface;
        group->length = static_cast<int>(surface.size());
        for (const Candidate& candidate : candidates) {
            word::Record record;
            record.target = candidate.target;
            record.partCode = candidate.part;
            group->entries.push_back(worker.NewEntry(record));
        }
        return group;
    }

    word::Item& Add(std::string_view surface, std::initializer_list<Candidate> candidates,
                    word::Selection choice = {word::Provenance::FirstXxx}, int selected = 0) {
        word::Group* group = MakeGroup(surface, candidates);
        word::Item* it = worker.NewItem(worker.NewDetail(word::kDetailInfo, 0), group, group->surface, 0, 0);
        it->SelectCandidate(selected, choice);
        it->prev = worker.chain.tail;
        if (it->prev != nullptr) it->prev->next = it;
        else worker.chain.head = it;
        worker.chain.tail = it;
        return *it;
    }
};

void CheckChoice(word::Selection choice, word::Provenance layer, int id = -1) {
    HD_CHECK_EQ(static_cast<int>(choice.layer), static_cast<int>(layer));
    HD_CHECK_EQ(choice.id, id);
}

void CheckTargets(const word::Item& it, std::initializer_list<std::string_view> targets) {
    HD_CHECK_EQ(it.group->entries.size(), targets.size());
    int slot = 0;
    for (const auto target : targets) HD_CHECK_EQ(std::string(it.EntryText(slot++)), std::string(target));
}

int EqualScore(const word::Item&, int, const word::SelectContext&) { return 0; }

int PopularityScore(const word::Item& it, int candidate, const word::SelectContext&) {
    return it.EntryText(candidate) == "인기" ? 10 : 0;
}

int ReputationScore(const word::Item& it, int candidate, const word::SelectContext&) {
    return it.EntryText(candidate) == "명성" ? 100 : 0;
}

int JotScore(const word::Item& it, int candidate, const word::SelectContext& context) {
    return &context.snapshot == &it.w->snapshot && &context.chain == &it.w->chain &&
           it.EntryText(candidate) == "적" ? 100 : 0;
}

}  // namespace

HD_TEST(word_select_empty_scorers) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    word::Settings settings = word::Settings::Reference();
    HD_CHECK(settings.scorers.empty());
    HD_CHECK(!settings.selectShadow);
    HD_CHECK(!settings.selectRules);
    SelectLine line(*snapshot, settings);
    word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}}, {word::Provenance::Digit, 17});
    word::Select(line.flusher);
    HD_CHECK_EQ(it.selected, 0);
    CheckChoice(it.choice, word::Provenance::Digit, 17);
    CheckTargets(it, {"유명세", "인기"});
    HD_CHECK(it.shadowChoices.empty());
}

HD_TEST(word_select_eligible_layers) {
    struct Case {
        word::Provenance layer;
        bool defaults;
        bool rules;
    };
    const Case cases[] = {
        {word::Provenance::FirstXxx, true, true},
        {word::Provenance::Digit, true, true},
        {word::Provenance::LocalRule, false, true},
        {word::Provenance::GlobalRule, false, true},
        {word::Provenance::Fixed, false, false},
        {word::Provenance::Sense, false, false},
        {word::Provenance::SenseDefault, false, false},
        {word::Provenance::Single, false, false},
        {word::Provenance::Rewrite, false, false},
        {word::Provenance::Initial, false, false},
        {word::Provenance::Scorer, false, false},
        {word::Provenance::None, false, false},
    };
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}};
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const Case& c : cases) {
        for (bool rules : {false, true}) {
            word::Settings settings = word::Settings::Reference();
            settings.scorers = scorers;
            settings.selectRules = rules;
            SelectLine line(*snapshot, settings);
            word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}}, {c.layer, 41});
            word::Select(line.flusher);
            const bool changed = rules ? c.rules : c.defaults;
            HD_CHECK_EQ(it.selected, changed ? 1 : 0);
            CheckChoice(it.choice, changed ? word::Provenance::Scorer : c.layer, changed ? 0 : 41);
            CheckTargets(it, {"유명세", "인기"});
            for (const word::Entry* entry : it.group->entries) CheckChoice(entry->targetChoice, word::Provenance::None);
            HD_CHECK(it.shadowChoices.empty());
        }
    }
}

HD_TEST(word_select_senses_run_first) {
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}};
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    HD_REQUIRE(snapshot->Senses().contains("声"));
    for (bool triggered : {false, true}) {
        for (bool matching : {false, true}) {
            for (bool scoring : {false, true}) {
                word::Settings settings = word::Settings::Reference();
                if (scoring) settings.scorers = scorers;
                settings.selectRules = true;
                SelectLine line(*snapshot, settings);
                if (triggered) {
                    line.Add("彼女", {{"그녀"}}, {word::Provenance::Single});
                    line.Add("の", {{"의", "P200"}}, {word::Provenance::Single});
                }
                word::Item& it = matching
                    ? line.Add("声", {{"소리"}, {"목소리"}, {"음"}}, {word::Provenance::LocalRule, 2})
                    : line.Add("声", {{"소리"}, {"음"}}, {word::Provenance::LocalRule, 2});
                word::Select(line.flusher);
                const word::Provenance layer = triggered ? word::Provenance::Sense : word::Provenance::SenseDefault;
                const int row = triggered ? 107 : 922;
                HD_CHECK_EQ(it.selected, matching ? 1 : 0);
                CheckChoice(it.choice, layer, row);
                if (matching) CheckTargets(it, {"소리", "목소리", "음"});
                else CheckTargets(it, {"목소리", "음"});
                CheckChoice(it.group->At(it.selected)->targetChoice,
                            matching ? word::Provenance::None : layer, matching ? -1 : row);
                HD_CHECK(it.shadowChoices.empty());
            }
        }
    }
}

HD_TEST(word_select_ties) {
    struct Case {
        std::initializer_list<Candidate> candidates;
        int current;
        int selected;
    };
    const Case cases[] = {
        {{{"인기"}, {"명성"}, {"평판"}}, 2, 2},
        {{{"유명세"}, {"인기"}, {"명성"}}, 0, 1},
        {{{"인기"}, {"명성"}, {"유명세"}}, 2, 0},
    };
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}};
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const Case& c : cases) {
        word::Settings settings = word::Settings::Reference();
        settings.scorers = scorers;
        SelectLine line(*snapshot, settings);
        word::Item& it = line.Add("人気", c.candidates, {word::Provenance::Digit, 23}, c.current);
        word::Select(line.flusher);
        HD_CHECK_EQ(it.selected, c.selected);
        const bool changed = c.current != c.selected;
        CheckChoice(it.choice, changed ? word::Provenance::Scorer : word::Provenance::Digit, changed ? 0 : 23);
    }
}

HD_TEST(word_select_scorer_order) {
    const word::Scorer equal{"equal", EqualScore};
    const word::Scorer popularity{"popularity", PopularityScore};
    const word::Scorer reputation{"reputation", ReputationScore};
    struct Case {
        word::Scorer scorers[3];
        int selected;
        int scorer;
    };
    const Case cases[] = {
        {{equal, popularity, reputation}, 1, 1},
        {{popularity, reputation, equal}, 1, 0},
        {{reputation, popularity, equal}, 2, 0},
        {{equal, equal, equal}, 0, -1},
        {{{"disabled", nullptr}, equal, popularity}, 1, 2},
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const Case& c : cases) {
        word::Settings settings = word::Settings::Reference();
        settings.scorers = c.scorers;
        SelectLine line(*snapshot, settings);
        word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}, {"명성"}});
        word::Select(line.flusher);
        HD_CHECK_EQ(it.selected, c.selected);
        CheckChoice(it.choice, c.scorer < 0 ? word::Provenance::FirstXxx : word::Provenance::Scorer, c.scorer);
    }
}

HD_TEST(word_select_shadow_all_scorers_without_mutation) {
    const word::Scorer scorers[] = {
        {"equal", EqualScore}, {"shortest", word::ShortestTargetScore}, {"jot", JotScore},
    };
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    word::Settings settings = word::Settings::Reference();
    settings.scorers = scorers;
    settings.selectShadow = true;
    SelectLine line(*snapshot, settings);
    word::Item& it = line.Add("書", {{"기입하", "M421"}, {"적", "M422"}, {"기록하", "M421"}},
                              {word::Provenance::Digit, 19}, 2);
    it.AssignEntrySurface(0, "쓰", {word::Provenance::Rewrite, 8});
    it.detail->cls = word::kDetailVerb;
    it.detail->group = line.MakeGroup("く", {{"4다", "S500"}, {"4는다", "S500"}});
    it.SetDirectIndex(1, {word::Provenance::GlobalRule, 37});
    it.compDI = 1;
    it.compUmiDI = 0;
    word::Select(line.flusher);
    HD_CHECK_EQ(it.selected, 2);
    CheckChoice(it.choice, word::Provenance::Digit, 19);
    CheckTargets(it, {"쓰", "적", "기록하"});
    CheckChoice(it.group->At(0)->targetChoice, word::Provenance::Rewrite, 8);
    CheckChoice(it.group->At(1)->targetChoice, word::Provenance::None);
    CheckChoice(it.group->At(2)->targetChoice, word::Provenance::None);
    HD_CHECK_EQ(it.compDI, 1);
    HD_CHECK_EQ(it.compUmiDI, 0);
    HD_CHECK_EQ(it.DirectIndex(), 1);
    CheckChoice(it.directChoice, word::Provenance::GlobalRule, 37);
    HD_CHECK_EQ(std::string(it.DirectText(0)), std::string("4다"));
    HD_CHECK_EQ(std::string(it.DirectText(1)), std::string("4는다"));
    for (const word::Entry* entry : it.DirectGroup()->entries) CheckChoice(entry->targetChoice, word::Provenance::None);
    HD_REQUIRE(it.shadowChoices.size() == 3);
    struct Expected {
        const char* name;
        int candidate;
        int currentScore;
        int score;
        bool winner;
        const char* target;
    };
    const Expected expected[] = {
        {"equal", 2, 0, 0, false, "기록하"},
        {"shortest", 0, -3, -1, true, "쓰"},
        {"jot", 1, 0, 100, false, "적"},
    };
    for (std::size_t i = 0; i < it.shadowChoices.size(); i++) {
        const auto& choice = it.shadowChoices[i];
        const auto& want = expected[i];
        HD_CHECK_EQ(std::string(choice.scorer), std::string(want.name));
        HD_CHECK_EQ(choice.from, 2);
        HD_CHECK_EQ(choice.candidate, want.candidate);
        HD_CHECK_EQ(choice.currentScore, want.currentScore);
        HD_CHECK_EQ(choice.score, want.score);
        HD_CHECK(choice.eligible);
        HD_CHECK_EQ(choice.winner, want.winner);
        HD_CHECK_EQ(choice.target, std::string(want.target));
    }
}

HD_TEST(word_select_shadow_blocked_recommendations) {
    const word::Provenance layers[] = {
        word::Provenance::LocalRule, word::Provenance::GlobalRule,
        word::Provenance::Sense, word::Provenance::SenseDefault, word::Provenance::Single,
        word::Provenance::Rewrite, word::Provenance::None,
    };
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}, {"popularity", PopularityScore}};
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (word::Provenance layer : layers) {
        for (bool rules : {false, true}) {
            word::Settings settings = word::Settings::Reference();
            settings.scorers = scorers;
            settings.selectShadow = true;
            settings.selectRules = rules;
            SelectLine line(*snapshot, settings);
            word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}}, {layer, 43});
            word::Select(line.flusher);
            HD_CHECK_EQ(it.selected, 0);
            CheckChoice(it.choice, layer, 43);
            CheckTargets(it, {"유명세", "인기"});
            HD_REQUIRE(it.shadowChoices.size() == 2);
            const bool eligible = rules && (layer == word::Provenance::LocalRule || layer == word::Provenance::GlobalRule);
            for (std::size_t i = 0; i < it.shadowChoices.size(); i++) {
                const auto& choice = it.shadowChoices[i];
                HD_CHECK_EQ(std::string(choice.scorer), std::string(scorers[i].name));
                HD_CHECK_EQ(choice.from, 0);
                HD_CHECK_EQ(choice.candidate, 1);
                HD_CHECK_EQ(choice.currentScore, i == 0 ? -3 : 0);
                HD_CHECK_EQ(choice.score, i == 0 ? -2 : 10);
                HD_CHECK_EQ(choice.eligible, eligible);
                HD_CHECK_EQ(choice.winner, eligible && i == 0);
                HD_CHECK_EQ(choice.target, std::string("인기"));
            }
        }
    }
}

HD_TEST(word_select_skips_fixed_invalid_and_small_groups) {
    struct Case {
        int count;
        int selected;
        bool fixed;
    };
    const Case cases[] = {{2, 0, true}, {2, -1, false}, {2, 2, false}, {0, 0, false}, {1, 0, false}};
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}};
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const Case& c : cases) {
        for (bool shadow : {false, true}) {
            word::Settings settings = word::Settings::Reference();
            settings.scorers = scorers;
            settings.selectShadow = shadow;
            settings.selectRules = true;
            SelectLine line(*snapshot, settings);
            word::Item& it = line.Add("人気", {{"유명세"}, {"인기"}}, {word::Provenance::FirstXxx, 29}, c.selected);
            it.group->At(0)->fixed = c.fixed;
            it.group->entries.resize(static_cast<std::size_t>(c.count));
            word::Select(line.flusher);
            HD_CHECK_EQ(it.selected, c.selected);
            CheckChoice(it.choice, word::Provenance::FirstXxx, 29);
            HD_CHECK(it.shadowChoices.empty());
        }
    }
}

HD_TEST(word_select_same_class_digit_only) {
    const word::Scorer scorers[] = {{"shortest", word::ShortestTargetScore}};
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (bool shadow : {false, true}) {
        for (bool compatible : {false, true}) {
            word::Settings settings = word::Settings::Reference();
            settings.scorers = scorers;
            settings.selectShadow = shadow;
            SelectLine line(*snapshot, settings);
            word::Item& it = line.Add("人気", {{"유명세", "N110"}, {"인", "M421"}, {"", ""}, {"인기", "N130"}});
            if (!compatible) it.group->entries.resize(3);
            word::Select(line.flusher);
            const int best = compatible ? 3 : 0;
            HD_CHECK_EQ(it.selected, shadow ? 0 : best);
            CheckChoice(it.choice, !shadow && compatible ? word::Provenance::Scorer : word::Provenance::FirstXxx,
                        !shadow && compatible ? 0 : -1);
            if (shadow) {
                HD_REQUIRE(it.shadowChoices.size() == 1);
                const auto& choice = it.shadowChoices[0];
                HD_CHECK_EQ(choice.from, 0);
                HD_CHECK_EQ(choice.candidate, best);
                HD_CHECK_EQ(choice.currentScore, -3);
                HD_CHECK_EQ(choice.score, compatible ? -2 : -3);
                HD_CHECK(choice.eligible);
                HD_CHECK_EQ(choice.winner, compatible);
            } else HD_CHECK(it.shadowChoices.empty());
        }
    }
}

HD_TEST(word_select_shortest_target_counts_scalars_without_mutation) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    SelectLine line(*snapshot);
    word::Item& it = line.Add("文字", {{"한국어"}, {"가"}, {"가"}, {"한\U00020000"}, {""}},
                              {word::Provenance::Digit, 31}, 1);
    it.compDI = 3;
    const word::SelectContext context{*snapshot, line.worker.chain};
    const int expected[] = {-3, -1, -2, -2, 0};
    for (int candidate = 0; candidate < 5; candidate++) {
        HD_CHECK_EQ(word::ShortestTargetScore(it, candidate, context), expected[candidate]);
    }
    HD_CHECK_EQ(word::ShortestTargetScore(it, -1, context), 0);
    HD_CHECK_EQ(word::ShortestTargetScore(it, 5, context), 0);
    HD_CHECK_EQ(it.selected, 1);
    CheckChoice(it.choice, word::Provenance::Digit, 31);
    HD_CHECK_EQ(it.compDI, 3);
    CheckTargets(it, {"한국어", "가", "가", "한\U00020000", ""});
    for (const word::Entry* entry : it.group->entries) CheckChoice(entry->targetChoice, word::Provenance::None);
    CheckChoice(it.directChoice, word::Provenance::None);
    HD_CHECK(it.shadowChoices.empty());
}
