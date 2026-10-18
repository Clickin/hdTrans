#include "hdTrans/word/analyze.h"

#include <algorithm>

#include "hdTrans/word/worker.h"

namespace hdTrans::word {

Analysis::Analysis() = default;
Analysis::~Analysis() = default;

void Analyze(const tokenizer::Tokens& tokens, const dict::Snapshot& snapshot, const dict::LookupContext& lookup,
             const Settings& settings, Arena& arena, Analysis& out, LookupObserver observer) {
    arena.Reset();
    out.sentences.clear();
    out.fault = false;
    out.worker = std::make_unique<Worker>(tokens, snapshot, lookup, settings, arena, observer);
    Worker& w = *out.worker;
    const auto& markers = tokens.markers;
    int first = 0;
    for (int i = 0; i < w.TokenCount(); i++) {
        int last = i;
        const bool end = w.Dispatch(i, last);
        if (w.fault) {
            out.fault = true;
            break;
        }
        if (end) {
            Sentence& s = out.sentences.emplace_back();
            s.first = first;
            s.last = last;
            s.chain = w.chain;
            auto byToken = [](const tokenizer::ControlMarker& m, int t) { return m.tokenIndex < t; };
            s.markerBegin = static_cast<int>(std::lower_bound(markers.begin(), markers.end(), first, byToken) - markers.begin());
            s.markerEnd = static_cast<int>(std::lower_bound(markers.begin(), markers.end(), last + 1, byToken) - markers.begin());
            s.current = w.current;
            s.transformSurface = w.transformSurface;
            w.chain = {};
            first = last + 1;
        }
        i = last;
    }
    out.spaced = w.spaced;
}

}  // namespace hdTrans::word
