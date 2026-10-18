#include "hdTrans/engine/driver.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "hdTrans/engine/glue.h"
#include "hdTrans/engine/resolve.h"
#include "hdTrans/engine/rules.h"
#include "hdTrans/names/document.h"
#include "hdTrans/output/cleanup.h"
#include "hdTrans/output/compose.h"
#include "hdTrans/pipeline/normalize.h"
#include "hdTrans/pipeline/protect.h"
#include "hdTrans/pipeline/restore.h"
#include "hdTrans/pipeline/segment.h"
#include "hdTrans/pipeline/sound.h"
#include "hdTrans/tokenizer/tokenize.h"
#include "hdTrans/text/lines.h"

namespace hdTrans::internal {

namespace {

// The buffers of one translation, reused from chunk to chunk.
struct Scratch {
    tokenizer::Tokens tokens;
    word::Arena arena;
    std::vector<output::ResolvedItem> items;
    std::vector<output::TokenText> texts;
    std::vector<tokenizer::ControlMarker> markers;
    output::Composer composer;
    word::SessionSenses* sessionSenses = nullptr;
};

// The pieces of the output as the tokenizer reads them (the span kinds are the same numbers).
std::vector<tokenizer::Piece> TokenizerPieces(const output::Pieces& pieces) {
    std::vector<tokenizer::Piece> out;
    out.reserve(pieces.size());
    for (const output::Piece& p : pieces) out.push_back({p.text, static_cast<int>(p.span)});
    return out;
}

// The token texts the compose stage reads for the sentence first..last. Tokens after the
// sentence are as scanned; the tokens of the sentence are as the token loop left them, and
// a wide space that the analysis reads as a space becomes one.
void BuildTexts(const tokenizer::Tokens& tokens, const word::Analysis& analysis, int first, int last,
                std::vector<output::TokenText>& texts) {
    if (texts.size() != tokens.size()) {
        texts.assign(tokens.size(), {});
        for (std::size_t j = 0; j < tokens.size(); j++) {
            texts[j].type = tokens[j].type;
            texts[j].span = tokens[j].span;
            texts[j].text = std::string(tokens.Raw(j));
            if (tokens[j].span != 0) texts[j].spanText = std::string(tokens.Raw(j));
        }
    }
    for (int j = first; j <= last; j++) {
        const auto v = tokens.View(static_cast<std::size_t>(j), static_cast<std::size_t>(last));
        texts[static_cast<std::size_t>(j)].type = v.type;
        texts[static_cast<std::size_t>(j)].text = std::string(v.text);
        if (analysis.spaced[static_cast<std::size_t>(j)]) {
            texts[static_cast<std::size_t>(j)].type = tokenizer::kTypeWideSpace;
            texts[static_cast<std::size_t>(j)].text = " ";
        }
    }
    if (static_cast<std::size_t>(last) + 1 < tokens.size()) {
        const std::size_t next = static_cast<std::size_t>(last) + 1;
        const auto v = tokens.View(next, static_cast<std::size_t>(last));
        texts[next].type = v.type;
        texts[next].text = std::string(v.text);
    }
}

output::Settings ComposeSettings(bool text) {
    output::Settings s;
    s.cleanup = text;
    s.text = text;
    return s;
}

word::Settings AnalysisSettings(bool text, bool verse, const TranslationOptions& options,
                                word::SessionSenses* sessionSenses) {
    word::Settings s = options.reference ? word::Settings::Reference() : word::Settings{};
    if (!options.reference) s.improvements = options.wordImprovements;
    s.text = text;
    s.terminal = text;
    s.verse = verse;
    s.scorers = options.scorers;
    s.selectShadow = options.selectShadow;
    s.selectRules = options.selectRules;
    s.sessionSenses = text ? sessionSenses : nullptr;
    return s;
}

// Translates one chunk: its tokens, the word analysis, then per sentence the resolve stage
// and the compose stage. The sentences are appended to out with the chunk index.
void TranslateChunk(const dict::Snapshot& snap, const dict::LookupContext& lookup, const output::Pieces& chunk,
                    int index, bool text, bool verse, Scratch& s, Generated& generated,
                    const StageObserver& observer, const TranslationOptions& options, std::vector<output::Sentence>& out) {
    s.texts.clear();
    const std::vector<tokenizer::Piece> pieces = TokenizerPieces(chunk);
    tokenizer::TokenizeSpans(pieces, text, s.tokens);
    if (observer.tokens) observer.tokens(observer.context, index, s.tokens);

    word::Analysis analysis;
    const word::Settings settings = AnalysisSettings(text, verse, options, s.sessionSenses);
    word::Analyze(s.tokens, snap, lookup, settings, s.arena, analysis);
    if (observer.analysis) observer.analysis(observer.context, index, s.tokens, analysis);

    const output::Settings composeSettings = ComposeSettings(text);
    for (std::size_t k = 0; k < analysis.sentences.size(); k++) {
        const word::Sentence& sentence = analysis.sentences[k];
        output::Sentence done;
        done.chunk = index;
        {
            std::lock_guard<std::mutex> lock(generated.mu);
            ResolveSentence(RulesProgram(), generated.families, analysis, k);
            if (observer.resolved) observer.resolved(observer.context, index, static_cast<int>(k), sentence.chain);
            ResolvedItems(sentence.chain, s.items);
            BuildTexts(s.tokens, analysis, sentence.first, sentence.last, s.texts);
            s.markers.clear();
            for (const tokenizer::ControlMarker& m : s.tokens.markers) {
                if (m.tokenIndex >= sentence.first && m.tokenIndex <= sentence.last) s.markers.push_back(m);
            }
            s.composer.Compose(s.items, s.texts, sentence.first, sentence.last, s.markers, composeSettings,
                               generated.families, done.pieces);
        }
        if (observer.composed) observer.composed(observer.context, index, static_cast<int>(k), done.pieces);
        out.push_back(std::move(done));
    }
}

// One line through every stage of the text path; the line's trailing "\r" is written
// back by the caller.
std::string TranslateBody(const dict::Snapshot& snap, pipeline::DocumentContext& doc, std::string_view body,
                          Scratch& s, Generated& generated, const StageObserver& observer, const TranslationOptions& options) {
    const pipeline::LineFacts facts = pipeline::ReadLine(doc, body);
    if (facts.wholeLine) return *facts.wholeLine;
    const pipeline::LineLookup lookup = pipeline::LookupOf(doc, facts);
    pipeline::LineContext ctx = pipeline::ContextOf(doc, lookup);
    ctx.exclamationNa = !options.reference && options.wordImprovements.exclamationNa;
    ctx.dialectForms = !options.reference && options.wordImprovements.dialectForms;

    const std::string normalized = pipeline::Normalize(facts.text, ctx);
    if (observer.text) observer.text(observer.context, "N", normalized);
    const std::string sounded = pipeline::Sound(normalized, ctx, snap);
    if (observer.text) observer.text(observer.context, "SN", sounded);

    const output::Pieces typed = pipeline::ProtectTyped(sounded, ctx, snap);
    const output::Pieces marked = pipeline::MarkCornerBrackets(typed);
    if (observer.pieces) observer.pieces(observer.context, "P", -1, marked);

    const std::vector<pipeline::Chunk> chunks = pipeline::Segment(marked);
    std::vector<output::Sentence> sentences;
    for (std::size_t i = 0; i < chunks.size(); i++) {
        if (observer.pieces) observer.pieces(observer.context, "CH", static_cast<int>(i), chunks[i]);
        TranslateChunk(snap, ctx.lookup, chunks[i], static_cast<int>(i), true, facts.verse, s, generated, observer, options,
                       sentences);
    }
    const output::Pieces cleaned = output::Cleanup(sentences);
    if (observer.cleaned) observer.cleaned(observer.context, cleaned);
    const pipeline::RestoreSteps steps = pipeline::RestoreAll(cleaned, typed, /*keepBrackets=*/true,
                                                              !options.reference && options.wordImprovements.literalCleanup);
    if (observer.restored) observer.restored(observer.context, steps);
    return steps.line;
}

}  // namespace

std::vector<std::string> TranslateDocument(const dict::Snapshot& snap, Generated& generated,
                                           std::span<const std::string> lines, Memory& memory,
                                           const StageObserver& observer, const TranslationOptions& options) {
    if (!snap.Healthy()) {
        const auto fallback = snap.Fallback();
        if (!fallback) return {};
        const auto replacement = NewGenerated(*fallback);
        return TranslateDocument(*fallback, *replacement, lines, memory, observer, options);
    }
    if (std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
            return line.size() > text::kMaxLineBytes;
        })) {
        std::vector<std::string> bounded;
        std::vector<std::size_t> ends;
        for (const std::string& line : lines) {
            for (std::string_view piece : text::TranslationPieces(line)) bounded.emplace_back(piece);
            ends.push_back(bounded.size());
        }
        const auto translated = TranslateDocument(snap, generated, bounded, memory, observer, options);
        if (translated.size() != bounded.size()) return {};
        std::vector<std::string> joined(lines.size());
        for (std::size_t i = 0, piece = 0; i < ends.size(); i++) {
            while (piece < ends[i]) joined[i] += translated[piece++];
        }
        return joined;
    }
    const std::unique_ptr<pipeline::DocumentContext> doc = pipeline::BuildDocument(
        snap, lines, !options.reference && options.wordImprovements.extraBuiltin, &memory.document);

    Scratch s;
    word::SessionSenses pending;
    if (!options.reference && (options.wordImprovements.documentSenses || options.selectShadow)) {
        if (snap.Mapped()) { pending = memory.senses; s.sessionSenses = &pending; }
        else s.sessionSenses = &memory.senses;
    }
    std::vector<std::string> out;
    out.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); i++) {
        if (observer.line) observer.line(observer.context, static_cast<int>(i));
        // ReadLine takes the line without its one trailing "\r"; the "\r" is written back.
        const bool cr = !lines[i].empty() && lines[i].back() == '\r';
        std::string translated = TranslateBody(snap, *doc, lines[i], s, generated, observer, options);
        if (s.sessionSenses != nullptr) s.sessionSenses->FinishLine();
        if (cr) translated += '\r';
        out.push_back(std::move(translated));
    }
    if (!snap.Healthy()) {
        const auto fallback = snap.Fallback();
        if (!fallback) return {};
        const auto replacement = NewGenerated(*fallback);
        return TranslateDocument(*fallback, *replacement, lines, memory, observer, options);
    }
    if (s.sessionSenses == &pending) memory.senses = std::move(pending);
    pipeline::RememberDocument(snap, lines, memory.document);
    return out;
}

std::string TranslateRawLine(const dict::Snapshot& snap, Generated& generated, std::string_view line,
                             const StageObserver& observer, const TranslationOptions& options) {
    if (!snap.Healthy()) {
        const auto fallback = snap.Fallback();
        if (!fallback) return {};
        const auto replacement = NewGenerated(*fallback);
        return TranslateRawLine(*fallback, *replacement, line, observer, options);
    }
    Scratch s;
    if (observer.line) observer.line(observer.context, 0);
    dict::LookupContext lookup;
    lookup.plain = &snap.Plain(!options.reference && options.wordImprovements.extraBuiltin);
    std::string result;
    for (std::string_view piece : text::TranslationPieces(line)) {
        output::Pieces chunk;
        if (!piece.empty()) chunk.push_back({std::string(piece), output::SpanText});
        std::vector<output::Sentence> sentences;
        TranslateChunk(snap, lookup, chunk, 0, false, false, s, generated, observer, options, sentences);
        output::Pieces composed;
        for (const output::Sentence& sentence : sentences) output::Append(composed, sentence.pieces);
        result += output::String(composed);
    }
    if (!snap.Healthy()) {
        const auto fallback = snap.Fallback();
        if (!fallback) return {};
        const auto replacement = NewGenerated(*fallback);
        return TranslateRawLine(*fallback, *replacement, line, observer, options);
    }
    return result;
}

}  // namespace hdTrans::internal
