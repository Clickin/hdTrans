#pragma once
// The analyze stage: the per-token word stage that turns the tokens of a chunk into
// the item chain of each sentence, up to the end-of-sentence passes.
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/tokenizer/token.h"
#include "hdTrans/word/item.h"

namespace hdTrans::word {

class Worker;

// The switches the word stage reads.
struct Settings {
    // Text input: the word choices and lookup filters of the text path apply.
    bool text = false;
    // The speech level (an output register, 0 for automatic) of sentence-final plain
    // and polite predicates; it selects the plain-dictionary end rows with a register.
    uint8_t registers[2] = {0, 0};
    int hanjaToHangul = 1;    // synthetic targets: kanji as their Hangul reading
    int deleteJapan = 1;      // synthetic targets: kana dropped
    int tokenizerOption = 1;  // a J2 ending matches at the end of the token text
};

// Called after every dictionary lookup with the token being processed (-1 before the
// first), the query, the mode and the candidates after the filters.
struct LookupObserver {
    void (*fn)(void* context, int token, std::string_view query, int mode, const Record* records) = nullptr;
    void* context = nullptr;
};

// One sentence of a chunk.
struct Sentence {
    int first = 0;  // its tokens
    int last = 0;
    Chain chain;
    // Its control markers: tokens.markers[markerBegin, markerEnd).
    int markerBegin = 0;
    int markerEnd = 0;
    // The lookup context at its end, which the lookups of the sentence passes keep:
    // the token being processed and the last Japanese token text segmented.
    int current = -1;
    std::string_view transformSurface;
};

// The sentences of a chunk.
struct Analysis {
    std::vector<Sentence> sentences;
    // Per token: a full-width space after a word with a "Wa" grammar code, which
    // reads as " " from then on, is never visited, and does not end the sentence.
    std::vector<uint8_t> spaced;
    // A katakana transliteration fault ended the chunk in the sentence after the
    // last one; the output of the chunk is that of the sentences before.
    bool fault = false;
    // The state the items refer to (tokens, settings, lookups), which the sentence
    // passes continue with.
    std::unique_ptr<Worker> worker;

    Analysis();
    ~Analysis();
    Analysis(const Analysis&) = delete;
    Analysis& operator=(const Analysis&) = delete;
};

// Runs the token loop over tokens (prepared by the tokenizer) and returns the item
// chain of every sentence before its sentence passes. Lookups take the plain
// entries, strategy, prior dictionary and name keys of lookup (its skip is replaced
// by the word stage's own filter). The items live in arena, which is reset first.
void Analyze(const tokenizer::Tokens& tokens, const dict::Snapshot& snapshot, const dict::LookupContext& lookup,
             const Settings& settings, Arena& arena, Analysis& out, LookupObserver observer = {});

}  // namespace hdTrans::word
