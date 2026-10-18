#pragma once

namespace hdTrans::dict {
class Snapshot;
}

namespace hdTrans::word {

class Item;
struct Chain;
struct Flusher;

struct SelectContext {
    const dict::Snapshot& snapshot;
    const Chain& chain;
};

// Scores read the resolved candidates without changing them. Larger scores win.
struct Scorer {
    const char* name;
    int (*score)(const Item&, int candidate, const SelectContext&);
};

// Sense rows run first; scorers may replace only a candidate-order default, or a
// rule choice when selectRules is set. Sense rows and fixed items stay protected.
void Select(Flusher& f);

// An opt-in diagnostic score: the negative number of Unicode scalars in the target.
int ShortestTargetScore(const Item& it, int candidate, const SelectContext& context);

}  // namespace hdTrans::word
