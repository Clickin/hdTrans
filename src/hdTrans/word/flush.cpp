#include "hdTrans/word/flush.h"

namespace hdTrans::word {

// Passes run in this order. Implemented: the raw-path passes. Pending, in their order:
// passiveRecords (text only, before cleanupSelectors), chooseSenses, chooseInitialConjunctions,
// goNotSay, hanBeforeNa, adjectiveNeighbours, geotBeforeMitai, positiveIke, copulaDeAru,
// negativeCopula, subjectBeforeGa, subjectNo, verseTe, teRequest, resultState, notYet, seeAnd,
// teSequence, pastSequence, quoteTo, nodaTo, terminalBeforeQuote, hodoDegree, questionCopula,
// genitiveBetweenNames, longVowelAfterKatakana, returnTo, karaSource, niAfterAdverb, noAfterNa,
// deScene, younaBeforeModifier, particleInNext, jiBeforeJiman, evenBeforePassive, comitativeTo,
// nounTo (text); preferTerminal (terminal), exclaimedAdjective (terminal and text),
// verbNegative (text).
void Flush(Flusher& f) {
    DrainNmTail(f);
    ReinsertPhrases(f);
    CleanupSelectors(f);
    CleanupClass3(f);
    ApplyNegatives(f);
}

}  // namespace hdTrans::word
