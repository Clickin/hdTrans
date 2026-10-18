#include "hdTrans/word/flush.h"

namespace hdTrans::word {

// The sentence passes in order. The text-path passes run only when settings.text is set.
void Flush(Flusher& f) {
    DrainNmTail(f);
    ReinsertPhrases(f);
    if (f.w.settings.text) PassiveRecords(f);
    CleanupSelectors(f);
    CleanupClass3(f);
    ApplyNegatives(f);
    if (f.w.settings.text) {
        ChooseSenses(f);
        ChooseInitialConjunctions(f);
        GoNotSay(f);
        HanBeforeNa(f);
        AdjectiveNeighbours(f);
        GeotBeforeMitai(f);
        PositiveIke(f);
        CopulaDeAru(f);
        NegativeCopula(f);
        SubjectBeforeGa(f);
        SubjectNo(f);
        VerseTe(f);
        TeRequest(f);
        ResultState(f);
        NotYet(f);
        SeeAnd(f);
        TeSequence(f);
        PastSequence(f);
        QuoteTo(f);
        NodaTo(f);
        TerminalBeforeQuote(f);
        HodoDegree(f);
        QuestionCopula(f);
        GenitiveBetweenNames(f);
        LongVowelAfterKatakana(f);
        ReturnTo(f);
        EvenBeforePassive(f);
    }
    if (f.w.settings.terminal) PreferTerminal(f);
}

}  // namespace hdTrans::word
