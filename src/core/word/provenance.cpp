#include "core/word/provenance.h"

namespace hdTrans::word {

const char* ProvenanceName(Provenance layer) {
    switch (layer) {
        case Provenance::None: return "None";
        case Provenance::Fixed: return "Fixed";
        case Provenance::LocalRule: return "LocalRule";
        case Provenance::GlobalRule: return "GlobalRule";
        case Provenance::Single: return "Single";
        case Provenance::FirstXxx: return "FirstXxx";
        case Provenance::Digit: return "Digit";
        case Provenance::Sense: return "Sense";
        case Provenance::SenseDefault: return "SenseDefault";
        case Provenance::Initial: return "Initial";
        case Provenance::Rewrite: return "Rewrite";
        case Provenance::Scorer: return "Scorer";
    }
    return "None";
}

}  // namespace hdTrans::word
