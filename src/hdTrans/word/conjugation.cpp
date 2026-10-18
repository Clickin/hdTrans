#include "hdTrans/word/conjugation.h"

namespace hdTrans::word {

std::string_view Conjugation(int kind, int branch) {
    if (branch < 0 || branch >= kConjugationBranches) return {};
    if (kind == kKindIku) {
        switch (branch) {
            case 6:
                return "った";
            case 7:
                return "って";
            case 17:
                return "っちゃ";
        }
        kind = 1;
    }
    if (kind < 0 || kind >= kConjugationKinds) return {};
    return internal::ConjugationRow(kind)[branch];
}

std::string_view PrefixForm(int index) {
    if (index < 0 || index >= kPrefixForms) return {};
    return internal::PrefixFormTable()[index];
}

}  // namespace hdTrans::word
