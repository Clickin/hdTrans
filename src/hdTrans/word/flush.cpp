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
        HanBeforeNa(f);
    }
    if (f.w.settings.terminal) PreferTerminal(f);
}

}  // namespace hdTrans::word
