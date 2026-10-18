#pragma once
// The driver: the stages of the pipeline in order, over the lines of a document (the text
// path) or over one line as a single piece (the raw path).
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/output/families.h"
#include "hdTrans/output/pieces.h"
#include "hdTrans/pipeline/restore.h"
#include "hdTrans/tokenizer/token.h"
#include "hdTrans/word/analyze.h"
#include "hdTrans/word/document_senses.h"

namespace hdTrans::internal {

// Receives the outputs of the stages as they are made. A null member is not called. The
// line callback starts each line before its stages; chunk indices restart per line.
struct StageObserver {
    void* context = nullptr;
    void (*line)(void* context, int line) = nullptr;
    // N (normalised) and SN (sounded) of a line.
    void (*text)(void* context, std::string_view tag, std::string_view text) = nullptr;
    // P (the protected pieces of a line, chunk -1) and CH (a chunk).
    void (*pieces)(void* context, std::string_view tag, int chunk, const output::Pieces& pieces) = nullptr;
    // The tokens of a chunk, after tokenizing.
    void (*tokens)(void* context, int chunk, const tokenizer::Tokens& tokens) = nullptr;
    // The sentences of a chunk after the word analysis, before resolve.
    void (*analysis)(void* context, int chunk, const tokenizer::Tokens& tokens, const word::Analysis& analysis) =
        nullptr;
    // The chain of a sentence after the resolve stage.
    void (*resolved)(void* context, int chunk, int sentence, const word::Chain& chain) = nullptr;
    // The output pieces of a sentence after the compose stage (K).
    void (*composed)(void* context, int chunk, int sentence, const output::Pieces& pieces) = nullptr;
    // The cleaned-up pieces of a line (C), then the restore steps of the line (R1-R3, OUT).
    void (*cleaned)(void* context, const output::Pieces& pieces) = nullptr;
    void (*restored)(void* context, const pipeline::RestoreSteps& steps) = nullptr;
};

// The marker families of one loaded dictionary, made by the translations of all sessions.
// Resolve and compose of a sentence run under mu.
struct Generated {
    std::mutex mu;
    output::Families families;
};

// The marker families of a loaded dictionary, with the families the dictionaries made at load.
std::shared_ptr<Generated> NewGenerated(const dict::Snapshot& snap);

// What a session keeps between its calls.
struct Memory {
    std::vector<std::string> lines;  // the lines of earlier calls, read by the name registry
    word::SessionSenses senses;
};

struct TranslationOptions {
    bool reference = false;
    std::span<const word::Scorer> scorers;
    bool selectShadow = false;
    bool selectRules = false;
};

// Translates the lines of one call as one document: the registry and the document context
// read the lines of memory and of this call. Returns the lines translated (each keeps its
// "\r"). memory receives the lines.
std::vector<std::string> TranslateDocument(const dict::Snapshot& snap, Generated& generated,
                                           std::span<const std::string> lines, Memory& memory,
                                           const StageObserver& observer, const TranslationOptions& options = {});

// Translates line (without "\n") as one piece with the word pipeline only.
std::string TranslateRawLine(const dict::Snapshot& snap, Generated& generated, std::string_view line,
                             const StageObserver& observer, const TranslationOptions& options = {});

}  // namespace hdTrans::internal
