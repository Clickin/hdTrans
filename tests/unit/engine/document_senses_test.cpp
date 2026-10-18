#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "hdTrans/engine.h"
#include "hdTrans/engine/driver.h"
#include "hdtest.h"
#include "golden.h"
#include "tokenizer/stage_records.h"

using namespace hdTrans;

namespace {

const std::string kRows =
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "東京\t동경\tN130\tQPNXXX\t\tGN\t0\t1\n"
    "東京\t도쿄\tN130\tQCT\t\tGN\t9\t2\n"
    "市\t시\tN110\tXXX\t\tGN\t0\t3\n";

std::shared_ptr<const dict::Snapshot> Dictionary() {
    const auto loaded = dict::Load({{0, "base.tsv", kRows}});
    HD_CHECK(loaded.errors.empty());
    return loaded.snapshot;
}

struct Choices {
    std::vector<word::Selection> choices;
    std::size_t scored = 0, wouldChange = 0, blocked = 0;
};

void Observe(void* context, int, int, const word::Chain& chain) {
    auto& observed = *static_cast<Choices*>(context);
    for (const word::Item* it = chain.head; it != nullptr; it = it->next) {
        if (it->group->surface == "東京") observed.choices.push_back(it->choice);
        for (const auto& choice : it->shadowChoices) {
            if (std::string_view(choice.scorer) != "document-senses") continue;
            observed.scored++;
            if (choice.winner) observed.wouldChange++;
            if (!choice.eligible && choice.candidate != choice.from) observed.blocked++;
        }
    }
}

std::vector<std::string> Lines(const std::string& input) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        const auto end = input.find('\n', start);
        lines.push_back(input.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) return lines;
        start = end + 1;
    }
}

struct DictionaryFolder {
    std::filesystem::path path;

    DictionaryFolder() {
        std::error_code error;
        const auto root = std::filesystem::temp_directory_path(error);
        if (error) return;
        path = root / ("hdtrans-document-senses-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(path, error)) {
            path.clear();
            return;
        }
        std::ofstream file(path / "base.tsv", std::ios::binary);
        file << kRows;
        HD_CHECK(file.good());
    }

    ~DictionaryFolder() {
        std::error_code error;
        if (!path.empty()) std::filesystem::remove_all(path, error);
    }
};

}  // namespace

HD_TEST(document_senses_two_line_rule_document) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    for (const bool reference : {false, true}) {
        const auto generated = internal::NewGenerated(*snapshot);
        internal::Memory memory;
        Choices observed;
        internal::StageObserver observer;
        observer.context = &observed;
        observer.resolved = Observe;
        internal::TranslationOptions options;
        options.reference = reference;
        options.wordImprovements.documentSenses = true;
        const std::string input[] = {"東京、", "東京。"};
        const auto out = internal::TranslateDocument(*snapshot, *generated, input, memory, observer, options);
        HD_REQUIRE(out.size() == 2);
        HD_CHECK_EQ(out[0], std::string("도쿄,"));
        HD_CHECK_EQ(out[1], std::string(reference ? "동경." : "도쿄."));
        HD_REQUIRE(observed.choices.size() == 2);
        HD_CHECK(observed.choices[0].layer == word::Provenance::LocalRule);
        HD_CHECK(observed.choices[1].layer == (reference ? word::Provenance::FirstXxx : word::Provenance::Scorer));
        HD_CHECK_EQ(memory.senses.choices.empty(), reference);
    }
}

HD_TEST(document_senses_no_same_line_or_future_learning) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    const auto generated = internal::NewGenerated(*snapshot);
    internal::Memory memory;
    internal::TranslationOptions options;
    options.wordImprovements.documentSenses = true;
    const std::string sameLine[] = {"東京、東京。"};
    const auto first = internal::TranslateDocument(*snapshot, *generated, sameLine, memory, {}, options);
    HD_CHECK_EQ(first[0], std::string("도쿄, 동경."));
    internal::Memory fresh;
    const std::string future[] = {"東京。", "東京、", "東京。"};
    const auto out = internal::TranslateDocument(*snapshot, *generated, future, fresh, {}, options);
    HD_REQUIRE(out.size() == 3);
    HD_CHECK_EQ(out[0], std::string("동경."));
    HD_CHECK_EQ(out[1], std::string("도쿄,"));
    HD_CHECK_EQ(out[2], std::string("도쿄."));
}

HD_TEST(document_senses_session_reset_isolation_and_reference) {
    DictionaryFolder dictionary;
    HD_REQUIRE(!dictionary.path.empty());
    Engine engine;
    HD_REQUIRE(engine.Open({dictionary.path.string()}));
    Session first(engine), independent(engine), reference(engine, true);
    HD_CHECK_EQ(first.Translate("東京、"), std::string("도쿄,"));
    HD_CHECK_EQ(first.Translate("東京。"), std::string("동경."));
    HD_CHECK_EQ(independent.Translate("東京。"), std::string("동경."));
    HD_CHECK_EQ(reference.Translate("東京、\n東京。"), std::string("도쿄,\n동경."));
    first.Reset();
    HD_CHECK_EQ(first.Translate("東京。"), std::string("동경."));
    HD_CHECK(engine.Reload());
    first.Reset();
    HD_CHECK_EQ(first.Translate("東京。"), std::string("동경."));
    Engine referenceEngine(true);
    HD_REQUIRE(referenceEngine.Open({dictionary.path.string()}));
    Session inherited(referenceEngine);
    HD_CHECK_EQ(inherited.Translate("東京、\n東京。"), std::string("도쿄,\n동경."));
}

HD_TEST(document_senses_shadow_consistency) {
    if (hdtest::GoldenDir().empty()) HD_SKIP("HDTRANS_GOLDEN is not set; configure the expectation folder");
    if (hdtest::DictsDir().empty()) HD_SKIP("HDTRANS_DICTS is not set; configure the dictionary folder");
    const char* root = std::getenv("HDTRANS_GOLDEN");
    if (root == nullptr || *root == 0) {
        HD_SKIP("no HDTRANS_GOLDEN");
    }
    const auto loaded = dict::LoadFolders({hdtest::DictsDir()});
    HD_REQUIRE(loaded.snapshot != nullptr && loaded.snapshot->HasDictionary());
    std::error_code error;
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (entry.path().extension() == ".in" && entry.path().stem().string().starts_with("doc-")) {
            paths.push_back(entry.path());
        }
    }
    HD_REQUIRE(!error && !paths.empty());
    std::sort(paths.begin(), paths.end());
    size_t expectedSets = 0;
    for (const auto& set : hdtest::ManifestSets(std::string(root) + "/stages/", ".S.tsv")) {
        if (set.starts_with("doc-")) ++expectedSets;
    }
    HD_REQUIRE(paths.size() == expectedSets);
    Choices total;
    for (const auto& path : paths) {
        std::string input;
        HD_REQUIRE(stagetest::ReadFile(path.string(), input));
        const auto lines = Lines(input);
        const auto generated = internal::NewGenerated(*loaded.snapshot);
        internal::Memory plainMemory, shadowMemory;
        internal::TranslationOptions defaults, shadow;
        shadow.selectShadow = true;
        Choices observed;
        internal::StageObserver observer;
        observer.context = &observed;
        observer.resolved = Observe;
        const auto expected = internal::TranslateDocument(*loaded.snapshot, *generated, lines, plainMemory, {}, defaults);
        const auto out = internal::TranslateDocument(*loaded.snapshot, *generated, lines, shadowMemory, observer, shadow);
        HD_REQUIRE(!expected.empty());
        HD_CHECK(out == expected);
        std::string frozen;
        HD_REQUIRE(stagetest::ReadFile((path.parent_path() / (path.stem().string() + ".expected")).string(), frozen));
        HD_REQUIRE(!frozen.empty());
        HD_CHECK_EQ(out.size(), Lines(frozen).size());
        internal::Memory referenceMemory;
        internal::TranslationOptions reference;
        reference.reference = true;
        reference.selectShadow = true;
        const auto referenceOut = internal::TranslateDocument(*loaded.snapshot, *generated, lines, referenceMemory, {}, reference);
        HD_CHECK(referenceOut == Lines(frozen));
        total.scored += observed.scored;
        total.wouldChange += observed.wouldChange;
        total.blocked += observed.blocked;
        std::printf("document_senses_shadow %s: scored=%zu would-change=%zu blocked=%zu\n",
                    path.stem().string().c_str(), observed.scored, observed.wouldChange, observed.blocked);
    }
    std::printf("document_senses_shadow total: sets=%zu scored=%zu would-change=%zu blocked=%zu\n",
                paths.size(), total.scored, total.wouldChange, total.blocked);
    HD_CHECK_EQ(total.scored, size_t{57168});
    HD_CHECK_EQ(total.wouldChange, size_t{2});
    HD_CHECK_EQ(total.blocked, size_t{4});
}

HD_TEST(document_senses_memory_calls_and_reset) {
    const auto snapshot = Dictionary();
    HD_REQUIRE(snapshot != nullptr);
    const auto generated = internal::NewGenerated(*snapshot);
    internal::TranslationOptions options;
    options.wordImprovements.documentSenses = true;
    internal::Memory memory, independent;
    const std::string seed[] = {"東京、"}, later[] = {"東京。"};
    internal::TranslateDocument(*snapshot, *generated, seed, memory, {}, options);
    HD_CHECK_EQ(internal::TranslateDocument(*snapshot, *generated, later, memory, {}, options)[0],
                std::string("도쿄."));
    HD_CHECK_EQ(internal::TranslateDocument(*snapshot, *generated, later, independent, {}, options)[0],
                std::string("동경."));
    memory = {};
    HD_CHECK_EQ(internal::TranslateDocument(*snapshot, *generated, later, memory, {}, options)[0],
                std::string("동경."));
    internal::Memory disabled;
    internal::TranslateDocument(*snapshot, *generated, seed, disabled, {});
    HD_CHECK(disabled.senses.choices.empty() && disabled.senses.pending.empty());
}
