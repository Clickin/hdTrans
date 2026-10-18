#include "hdTrans/engine/resolve.h"

#include "hdTrans/word/flush.h"

namespace hdTrans::internal {

void ResolveSentence(const rules::Program& program, const output::Families& families, word::Analysis& analysis,
                     std::size_t sentence) {
    word::Sentence& s = analysis.sentences[sentence];
    word::Worker& w = *analysis.worker;
    w.chain = s.chain;
    w.current = s.current;
    w.transformSurface = s.transformSurface;
    word::Flusher f{w, program, families};
    word::Flush(f);
    s.chain = w.chain;
}

}  // namespace hdTrans::internal
