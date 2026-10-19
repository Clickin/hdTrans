#pragma once
// The misread filter: a linear model over the candidates of the items that have another
// reading. It runs once per sentence, after every selection pass, and replaces a chosen
// candidate that it scores as a likely misreading by the best-scored alternative.
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/word/item.h"

namespace hdTrans::word {

struct Flusher;

// What a decision chooses: the entry of the item's group (Word) or of its ending group
// (Ending).
enum class MisreadDecisionKind : uint8_t { Word, Ending };

struct MisreadWeight {
    std::string_view feature;
    int32_t weight;  // in thousandths of a logit unit
};

// The weights sorted by feature (byte order), the bias, the score from which a chosen
// candidate is replaced, and the score an alternative must stay below.
struct MisreadModel {
    std::span<const MisreadWeight> weights;
    int32_t bias = 0;
    int32_t threshold = 0;
    int32_t keep = 0;
};

// The trained model (misread_model_gen.cpp).
const MisreadModel& BuiltinMisreadModel();

// What the features read about an item besides its entries: the characters next to it
// past spaces (crossing token ends; the text of the line before the chunk at its start)
// and whether nothing precedes it in its line.
struct MisreadContext {
    std::string_view before;
    std::string_view after;
    bool lineStart = false;
};

MisreadContext MisreadContextOf(const Item& it);

// The Korean of the item's surface read as a sound, when the item may be one: a word
// without conjugation spelled in one to four katakana, or in hiragana before katakana,
// a small tsu or a long-vowel mark, whose chosen Korean does not already sound like it.
// Empty otherwise.
std::string SoundReading(const Item& it, const MisreadContext& context);

struct MisreadCandidate {
    int slot = -1;              // the entry or ending entry; -1 for the sound reading
    std::string target;         // its Korean
    bool chosen = false;
    std::vector<std::string> features;
    int32_t score = 0;
};

// The candidates of one decision of an item: the chosen one first, then up to six
// alternatives in slot order with Korean of their own, then the sound reading. Every
// candidate's Korean is empty or has a Hangul syllable; a decision has an alternative.
struct MisreadDecision {
    Item* item = nullptr;
    MisreadDecisionKind kind = MisreadDecisionKind::Word;
    MisreadContext context;
    std::string sound;  // the sound reading, a Word alternative when not empty
    std::vector<MisreadCandidate> candidates;
};

// The decisions of every item of the chain that is not fixed, with features and scores
// under model, before any change.
std::vector<MisreadDecision> MisreadDecisions(const Chain& chain, const MisreadModel& model);

// The sum of the weights of features and the bias.
int32_t MisreadScore(const MisreadModel& model, std::span<const std::string> features);

// Whether a choice of this provenance may be replaced.
bool MisreadReplaceable(Provenance layer);

// The candidate that replaces the chosen one of decision, or null: the chosen candidate
// scores at least the threshold and its provenance may be replaced, and the alternative
// with the lowest score (the first on ties) scores below keep.
const MisreadCandidate* MisreadReplacement(const MisreadDecision& decision, const MisreadModel& model);

// The pass (Flush, misreadFilter): every replacement of the chain's decisions, decided
// before any is made.
void FilterMisreads(Flusher& f);

}  // namespace hdTrans::word
