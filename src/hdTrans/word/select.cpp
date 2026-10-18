#include "hdTrans/word/select.h"

#include <algorithm>
#include <limits>

#include "hdTrans/text/utf8.h"
#include "hdTrans/word/flush.h"
#include "hdTrans/word/document_senses.h"

namespace hdTrans::word {
namespace {

bool MaySelect(Provenance layer, bool rules) {
    switch (layer) {
        case Provenance::FirstXxx:
        case Provenance::Digit:
            return true;
        case Provenance::LocalRule:
        case Provenance::GlobalRule:
            return rules;
        default:
            return false;
    }
}

}  // namespace

void Select(Flusher& f) {
    ChooseSenses(f);
    const Settings& settings = f.w.settings;
    const bool documentSenses = (settings.improvements.documentSenses || settings.selectShadow) &&
                                settings.sessionSenses != nullptr;
    if (!documentSenses && settings.scorers.empty()) return;
    const Scorer documentScorer{"document-senses", DocumentSenseScore};
    const std::size_t documentCount = documentSenses ? 1 : 0;
    const SelectContext context{f.w.snapshot, f.w.chain};
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        const int count = static_cast<int>(it->group->entries.size());
        if (it->Fixed() || it->selected < 0 || it->selected >= count) continue;
        if (documentSenses) settings.sessionSenses->Remember(*it);
        if (count < 2) continue;
        if (!MaySelect(it->choice.layer, settings.selectRules) && !settings.selectShadow) continue;
        const int current = it->selected;
        const std::string_view code = it->Code(current);
        if (code.empty()) continue;
        bool decided = false;
        for (std::size_t index = 0; index < documentCount + settings.scorers.size(); index++) {
            const bool document = index < documentCount;
            const Scorer& scorer = document ? documentScorer : settings.scorers[index - documentCount];
            const bool eligible = MaySelect(it->choice.layer, !document && settings.selectRules);
            if (scorer.score == nullptr || (!eligible && !settings.selectShadow)) continue;
            const int currentScore = scorer.score(*it, current, context);
            int best = current, bestScore = currentScore;
            for (int candidate = 0; candidate < count; candidate++) {
                const std::string_view candidateCode = it->Code(candidate);
                if (candidate == current || candidateCode.empty() || candidateCode[0] != code[0]) continue;
                const int score = scorer.score(*it, candidate, context);
                // Starting at current preserves it on ties; other ties keep the first slot.
                if (score > bestScore) {
                    best = candidate;
                    bestScore = score;
                }
            }
            const bool winner = eligible && !decided && best != current;
            if (settings.selectShadow) {
                it->shadowChoices.push_back({scorer.name, current, best, currentScore, bestScore,
                                             eligible, winner, std::string(it->EntryText(best))});
                decided = decided || winner;
            } else if (winner) {
                it->SelectCandidate(best, {Provenance::Scorer, static_cast<int>(index)});
                break;
            }
        }
    }
}

int ShortestTargetScore(const Item& it, int candidate, const SelectContext&) {
    const std::size_t length = text::RuneCount(it.EntryText(candidate));
    return -static_cast<int>(std::min(length, static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

}  // namespace hdTrans::word
