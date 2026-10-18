#pragma once

#include <map>
#include <string>

#include "hdTrans/word/select.h"

namespace hdTrans::word {

struct SessionSense {
    std::string target;
    std::string partCode;
};

// Only choices from completed lines are visible to the scorer. The first trusted
// target for a key lasts until the session is reset.
struct SessionSenses {
    std::map<std::string, SessionSense, std::less<>> choices;
    std::map<std::string, SessionSense, std::less<>> pending;

    void FinishLine();
    void Remember(const Item& it);
};

int DocumentSenseScore(const Item& it, int candidate, const SelectContext& context);

}  // namespace hdTrans::word
