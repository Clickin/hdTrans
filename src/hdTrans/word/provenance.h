#pragma once
#include <cstdint>

namespace hdTrans::word {

enum class Provenance : uint8_t {
    None, Fixed, LocalRule, GlobalRule, Single, FirstXxx, Digit,
    Sense, SenseDefault, Initial, Rewrite, Scorer,
};

struct Selection {
    Provenance layer = Provenance::None;
    int id = -1;  // rule number, 1-based sense row, or scorer index
};

const char* ProvenanceName(Provenance layer);

}  // namespace hdTrans::word
