#pragma once

#include <map>
#include <string>
#include <string_view>

#include "hdTrans/word/select.h"

namespace hdTrans::word {

struct SessionSense {
    std::string target;
    std::string partCode;
};

// Only choices from completed lines are visible to the scorer. The first trusted
// target for a key lasts until reset or a between-call memory trim.
struct SessionSenses {
    std::map<std::string, SessionSense, std::less<>> choices;
    std::map<std::string, SessionSense, std::less<>> pending;

    void FinishLine();
    // Bounds cross-call choices; call only after translating the whole document.
    void Trim();
    void Remember(const Item& it);
};

// A view into target without its form code, marker suffix, auxiliary prefixes or
// common particles. This is a spelling comparison, not a Korean lemmatizer.
std::string_view DocumentSenseStem(std::string_view target);

int DocumentSenseScore(const Item& it, int candidate, const SelectContext& context);

}  // namespace hdTrans::word
