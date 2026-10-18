#pragma once
// The end-of-sentence passes of the word stage. They run over the item chain of one
// sentence (Worker::chain) in a fixed order and turn the analyzed items into the
// resolved chain: the replacements of katakana runs and noun phrases, the selection
// of each item's candidate and ending by the rules, and the passes that fix class-3
// words, negatives and terminal forms.
#include <string_view>

#include "hdTrans/output/families.h"
#include "hdTrans/rules/rules.h"
#include "hdTrans/word/worker.h"

namespace hdTrans::word {

// What the passes read besides the chain: the rule program of the selection and the
// marker families of the output lookups. The passes add the families they make (the
// conjugating targets of the text path), so the families are shared with the output stage.
struct Flusher {
    Worker& w;
    const rules::Program& program;
    output::Families& families;
};

// Runs the sentence passes over w.chain (the chain of one sentence after the token loop).
void Flush(Flusher& f);

// Selection (selectors.cpp).
void CleanupSelectors(Flusher& f);
void ApplyKj(Item* it);
bool IsStative(Flusher& f, Item* it);

// Terminal form (terminal.cpp).
void PreferTerminal(Flusher& f);

// Passes (passes.cpp).
void DrainNmTail(Flusher& f);
void ReinsertPhrases(Flusher& f);
void CleanupClass3(Flusher& f);
void ApplyNegatives(Flusher& f);

// Helpers of the text-path passes. TakeTarget (sense.cpp) selects the candidate of class
// with target, or writes target over the selected one. SelectLemma (grammar.cpp) selects
// the first entry whose dictionary form is lemma (or starts with prefix) and reports
// whether there was one. Thing and Person classify dictionary attributes.
void TakeTarget(Item* it, char cls, std::string_view target);
bool SelectLemma(Flusher& f, Item* it, std::string_view lemma);
bool SelectLemmaPrefix(Flusher& f, Item* it, std::string_view prefix);
bool Thing(std::string_view attr);
bool Person(std::string_view attr);

// Text-path grammar (grammar.cpp).
void PassiveRecords(Flusher& f);
void GoNotSay(Flusher& f);
void PositiveIke(Flusher& f);

// Text-path adjectival neighbours (neighbours.cpp).
void AdjectiveNeighbours(Flusher& f);
void GeotBeforeMitai(Flusher& f);

// Text-path senses and conjunctions (sense.cpp).
void ChooseSenses(Flusher& f);
void ChooseInitialConjunctions(Flusher& f);
void HanBeforeNa(Flusher& f);

}  // namespace hdTrans::word
