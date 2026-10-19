#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/engine/driver.h"

namespace hdTrans::internal {

struct TraceSelection {
    int index = -1;
    std::string text;
    std::string provenance;
    std::string layer;
    int id = -1;
    std::string source;
};

struct TraceEnding {
    TraceSelection choice;
    std::string surface;
};

struct TraceChoice {
    int line = 0;
    int chunk = 0;
    int sentence = 0;
    int token = 0;
    int start = 0;  // UTF-8 byte offset in the normalized token
    std::string surface;
    TraceSelection choice;
    std::optional<TraceEnding> ending;
};

struct TranslationTrace {
    std::string text;
    std::vector<TraceChoice> choices;
    std::vector<std::string> logs;
};

struct TraceCollector {
    TranslationTrace result;
    int line = 0;
    int lineBase = 0;
    StageObserver downstream;
};

// The observer is call-local; the collector owns all copied strings.
StageObserver TraceObserver(TraceCollector& collector, const StageObserver& downstream = {});

}  // namespace hdTrans::internal
