#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/engine/driver.h"
#include "core/engine/glue.h"
#include "core/text/utf8.h"
#include "core/word/misread.h"

namespace hdTrans::cli::stagerecords {

using Records = std::map<std::string, std::map<int, std::vector<std::string>>>;

struct Collector {
    Records records;
    int key = 0;
    int caseBase = 0;
    // The marker families that spell the dictionary forms of MC records; null for none.
    const output::Families* families = nullptr;
    internal::StageObserver forward;

    void Add(std::string record) {
        const std::string tag = record.substr(0, record.find('\t'));
        records[tag][key].push_back(std::move(record));
    }
};

// Text columns escape controls and invalid UTF-8 bytes as well as backslashes.
inline std::string Esc(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const auto r = text::DecodeRune(s.substr(i));
        const auto b = static_cast<unsigned char>(s[i]);
        if (r.cp == U'\\') {
            out += "\\\\";
        } else if (r.cp == U'\t') {
            out += "\\t";
        } else if (r.cp == U'\n') {
            out += "\\n";
        } else if (r.cp == U'\r') {
            out += "\\r";
        } else if (r.cp < 0x20 || r.cp == 0x7F || (r.cp == text::kReplacement && r.size == 1)) {
            constexpr char kHex[] = "0123456789abcdef";
            out += "\\x";
            out += kHex[b >> 4];
            out += kHex[b & 15];
        } else {
            out += s.substr(i, r.size);
        }
        i += r.size;
    }
    return out;
}

inline std::string PartCode(const word::Entry& e) {
    if (e.cls == 0) return std::string(e.code);
    return std::string(1, e.cls) + std::string(e.code);
}

inline void SentenceRecords(const word::Sentence& s, int chunk, int sent, std::vector<std::string>& out) {
    static const char* const kDetailNames[] = {"TWordInfo", "TVerbWord", "TKeiyoWord", "TDakeiyoWord"};
    const std::string c = std::to_string(chunk), n = std::to_string(sent);
    for (const word::Item* it = s.chain.head; it != nullptr; it = it->next) {
        const std::string at = std::to_string(it->tokenIndex) + ":" + std::to_string(it->start);
        out.push_back("L\t" + c + "\t" + n + "\t" + at + "\t" + std::to_string(it->length) + "\t" + Esc(it->group->surface) +
                      "\t" + std::to_string(it->group->entries.size()) + "\t" + kDetailNames[it->detail->cls] + "\t" +
                      std::to_string(it->DetailKind()) + "\t" + Esc(it->DirectSurface()) + "\t" +
                      std::to_string(it->DirectCount()));
        int k = 0;
        for (const word::Entry* e : it->group->entries) {
            out.push_back("LC\t" + c + "\t" + n + "\t" + at + "\t" + std::to_string(k++) + "\t" + Esc(PartCode(*e)) + "\t" +
                          Esc(e->attr) + "\t" + Esc(e->gattr) + "\t" + Esc(e->field) + "\t" + std::to_string(e->ruleId) +
                          "\t" + (e->fixed ? "1" : "0") + "\t" + Esc(e->dst));
        }
        for (int d = 0; d < it->DirectCount(); d++) {
            const word::Entry& e = *it->DirectEntry(d);
            out.push_back("LD\t" + c + "\t" + n + "\t" + at + "\t" + std::to_string(d) + "\t" +
                          (it->DirectEnabled(d) ? "1" : "0") + "\t" + Esc(PartCode(e)) + "\t" + Esc(e.attr) + "\t" +
                          Esc(e.gattr) + "\t" + Esc(e.field) + "\t" + std::to_string(e.ruleId) + "\t" + Esc(e.dst));
        }
    }
}

inline void OnLine(void* context, int line) {
    auto* c = static_cast<Collector*>(context);
    c->key = c->caseBase + line + 1;
    if (c->forward.line) c->forward.line(c->forward.context, line);
}

inline void OnText(void* context, std::string_view tag, std::string_view text) {
    static_cast<Collector*>(context)->Add(std::string(tag) + "\t" + Esc(text));
}

inline void OnPieces(void* context, std::string_view, int chunk, const output::Pieces& pieces) {
    auto* c = static_cast<Collector*>(context);
    for (size_t i = 0; i < pieces.size(); i++) {
        const std::string span = std::to_string(static_cast<int>(pieces[i].span));
        if (chunk < 0) {
            c->Add("P\t" + std::to_string(i) + "\t" + span + "\t" + Esc(pieces[i].text));
        } else {
            c->Add("CH\t" + std::to_string(chunk) + "\t" + std::to_string(i) + "\t" + span + "\t" + Esc(pieces[i].text));
        }
    }
}

inline void OnTokens(void* context, int chunk, const tokenizer::Tokens& tokens) {
    auto* c = static_cast<Collector*>(context);
    for (size_t i = 0; i < tokens.size(); i++) {
        const std::string_view raw = tokens.Raw(i);
        std::string r = "T\t" + std::to_string(chunk) + "\t" + std::to_string(i) + "\t" +
                        std::to_string(tokens[i].type) + "\t" + std::to_string(tokens[i].span) + "\t" + Esc(raw) + "\t";
        if (tokens[i].span != 0) r += Esc(raw);
        c->Add(std::move(r));
    }
}

inline void OnAnalysis(void* context, int chunk, const tokenizer::Tokens& tokens, const word::Analysis& analysis) {
    auto* c = static_cast<Collector*>(context);
    std::vector<std::string> out;
    for (size_t s = 0; s < analysis.sentences.size(); s++) {
        const word::Sentence& sentence = analysis.sentences[s];
        for (int k = sentence.markerBegin; k < sentence.markerEnd; k++) {
            const tokenizer::ControlMarker& marker = tokens.markers[static_cast<size_t>(k)];
            c->Add("CM\t" + std::to_string(chunk) + "\t" + std::to_string(s) + "\t" +
                   std::to_string(marker.tokenIndex) + "\t" + std::to_string(marker.textLength) + "\t" +
                   std::to_string(marker.markerKind));
        }
        out.clear();
        SentenceRecords(sentence, chunk, static_cast<int>(s), out);
        for (std::string& r : out) c->Add(std::move(r));
    }
}

// Entry semantic codes are not carried by resolved items; their column is "-".
inline std::string SRecord(int chunk, int sent, const output::ResolvedItem& it) {
    static const char* const kDetail[] = {"TWordInfo", "TVerbWord", "TKeiyoWord", "TDakeiyoWord"};
    auto part = [](const output::ResolvedEntry& e, bool present) {
        return present ? Esc(e.code) : std::string("-");
    };
    const bool hasEntry = !it.entry.code.empty() || !it.entry.text.empty() || !it.entry.gattr.empty();
    const bool hasEnding = !it.ending.code.empty() || !it.ending.text.empty() || !it.ending.gattr.empty();
    const bool hasFirst = !it.first.code.empty() || !it.first.text.empty() || !it.first.gattr.empty();
    const std::string columns[] = {
        "S", std::to_string(chunk), std::to_string(sent), std::to_string(it.tokenIndex) + ":" + std::to_string(it.offset),
        std::to_string(it.length), Esc(it.surface), std::to_string(it.selected), std::to_string(it.selectionFlag),
        kDetail[static_cast<int>(it.detailClass)], std::to_string(it.detailKind), std::to_string(it.detailRecord),
        std::to_string(it.directIndex),
        part(it.entry, hasEntry), "-", hasEntry ? Esc(it.entry.gattr) : "-", hasEntry ? Esc(it.entry.text) : "-",
        part(it.ending, hasEnding), hasEnding ? Esc(it.ending.gattr) : "-", hasEnding ? Esc(it.ending.text) : "-",
        part(it.first, hasFirst), hasFirst ? Esc(it.first.gattr) : "-", hasFirst ? Esc(it.first.text) : "-",
        it.fixed ? "1" : "0", Esc(it.endingSurface), std::to_string(it.endingLength), it.ownedEnding ? "1" : "0",
        it.otherHangul ? "1" : "0",
    };
    std::string out;
    for (size_t i = 0; i < std::size(columns); i++) {
        if (i > 0) out += '\t';
        out += columns[i];
    }
    return out;
}

// The facts the misread features read (MI items, MC entries with their dictionary forms,
// MS sound readings) and the features and scores of each decision candidate (MF).
inline void MisreadRecords(Collector& c, int chunk, int sentence, const word::Chain& chain) {
    auto part = [](const word::Entry& e) { return std::string(1, e.cls == 0 ? '_' : e.cls) + std::string(e.code); };
    auto lemma = [&](const word::Entry& e) { return c.families ? Esc(output::Lemma(e.dst, *c.families)) : std::string(); };
    const std::string at = std::to_string(chunk) + "\t" + std::to_string(sentence) + "\t";
    int index = 0;
    std::map<const word::Item*, int> indices;
    for (const word::Item* it = chain.head; it != nullptr; it = it->next, index++) {
        indices[it] = index;
        const word::MisreadContext m = word::MisreadContextOf(*it);
        const std::string n = at + std::to_string(index) + "\t";
        c.Add("MI\t" + n + std::to_string(it->tokenIndex) + ":" + std::to_string(it->start) + "\t" +
              std::to_string(it->length) + "\t" + Esc(it->group->surface) + "\t" + Esc(m.before) + "\t" +
              Esc(m.after) + "\t" + (m.lineStart ? "1" : "0") + "\t" + std::to_string(it->detail->cls) + "\t" +
              (it->Fixed() ? "1" : "0") + "\t" + std::to_string(it->selected) + "\t" +
              word::ProvenanceName(it->choice.layer) + "\t" + Esc(it->DirectSurface()) + "\t" +
              std::to_string(it->DirectIndex()) + "\t" + word::ProvenanceName(it->directChoice.layer));
        int k = 0;
        for (const word::Entry* e : it->group->entries) {
            c.Add("MC\t" + n + "W\t" + std::to_string(k++) + "\t" + Esc(part(*e)) + "\t" + Esc(e->attr) + "\t" +
                  Esc(e->gattr) + "\t1\t" + Esc(e->dst) + "\t" + lemma(*e));
        }
        for (int d = 0; d < it->DirectCount(); d++) {
            const word::Entry& e = *it->DirectEntry(d);
            c.Add("MC\t" + n + "E\t" + std::to_string(d) + "\t" + Esc(part(e)) + "\t" + Esc(e.attr) + "\t" +
                  Esc(e.gattr) + "\t" + (it->DirectEnabled(d) ? "1" : "0") + "\t" + Esc(e.dst) + "\t" + lemma(e));
        }
        if (const std::string sound = word::SoundReading(*it, m); !sound.empty()) c.Add("MS\t" + n + Esc(sound));
    }
    for (const word::MisreadDecision& d : word::MisreadDecisions(chain, word::BuiltinMisreadModel())) {
        const std::string n = at + std::to_string(indices[d.item]) + "\t" +
                              (d.kind == word::MisreadDecisionKind::Word ? "W" : "E") + "\t";
        for (const word::MisreadCandidate& cand : d.candidates) {
            std::string r = "MF\t" + n + std::to_string(cand.slot) + "\t" + std::to_string(cand.score);
            for (const std::string& f : cand.features) r += "\t" + Esc(f);
            c.Add(std::move(r));
        }
    }
}

inline void OnResolved(void* context, int chunk, int sentence, const word::Chain& chain) {
    auto* c = static_cast<Collector*>(context);
    std::vector<output::ResolvedItem> items;
    internal::ResolvedItems(chain, items);
    for (const output::ResolvedItem& it : items) c->Add(SRecord(chunk, sentence, it));
    MisreadRecords(*c, chunk, sentence, chain);
    if (c->forward.resolved) c->forward.resolved(c->forward.context, chunk, sentence, chain);
}

inline void OnComposed(void* context, int chunk, int sentence, const output::Pieces& pieces) {
    auto* c = static_cast<Collector*>(context);
    for (size_t p = 0; p < pieces.size(); p++) {
        c->Add("K\t" + std::to_string(chunk) + "\t" + std::to_string(sentence) + "\t" + std::to_string(p) + "\t" +
               std::to_string(static_cast<int>(pieces[p].span)) + "\t" + Esc(pieces[p].text));
    }
}

inline void AddPieces(Collector& c, const char* tag, const output::Pieces& pieces) {
    for (size_t p = 0; p < pieces.size(); p++) {
        c.Add(std::string(tag) + "\t" + std::to_string(p) + "\t" + std::to_string(static_cast<int>(pieces[p].span)) +
              "\t" + Esc(pieces[p].text));
    }
}

inline void OnCleaned(void* context, const output::Pieces& pieces) {
    AddPieces(*static_cast<Collector*>(context), "C", pieces);
}

inline void OnRestored(void* context, const pipeline::RestoreSteps& steps) {
    auto* c = static_cast<Collector*>(context);
    AddPieces(*c, "R1", steps.spaced);
    AddPieces(*c, "R2", steps.bracketed);
    AddPieces(*c, "R3", steps.korean);
}

// Final output is recorded by the caller, including paths without a restore step.
inline void AddOutput(Collector& c, int line, std::string_view output) {
    c.records["OUT"][line] = {"OUT\t" + Esc(output)};
}

inline internal::StageObserver ObserverFor(Collector& c, const internal::StageObserver& forward) {
    c.forward = forward;
    internal::StageObserver o;
    o.context = &c;
    o.line = OnLine;
    o.text = OnText;
    o.pieces = OnPieces;
    o.tokens = OnTokens;
    o.analysis = OnAnalysis;
    o.resolved = OnResolved;
    o.composed = OnComposed;
    o.cleaned = OnCleaned;
    o.restored = OnRestored;
    if (forward.filterLog) {
        o.filterLog = [](void* context, std::string_view message) {
            const auto& collector = *static_cast<Collector*>(context);
            collector.forward.filterLog(collector.forward.context, message);
        };
    }
    return o;
}

inline bool Dump(const std::string& dir, const Collector& c) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        std::fprintf(stderr, "hdtrans: cannot create stage directory %s: %s\n", dir.c_str(), ec.message().c_str());
        return false;
    }
    for (const char* tag : {"N", "SN", "P", "CH", "T", "CM", "L", "LC", "LD", "S", "MI", "MC", "MS", "MF", "K", "C", "R1", "R2", "R3", "OUT"}) {
        const std::string path = (std::filesystem::path(dir) / (std::string("input.") + tag + ".got.tsv")).string();
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (f == nullptr) {
            std::fprintf(stderr, "hdtrans: cannot open stage file %s: %s\n", path.c_str(), std::strerror(errno));
            return false;
        }
        bool ok = true;
        const auto found = c.records.find(tag);
        if (found != c.records.end()) {
            for (const auto& [k, recs] : found->second) {
                if (std::fprintf(f, "#LINE\t%d\n", k) < 0) {
                    ok = false;
                    break;
                }
                for (const std::string& r : recs) {
                    if (std::fprintf(f, "%s\n", r.c_str()) < 0) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) break;
            }
        }
        const int closed = std::fclose(f);
        if (!ok || closed != 0) {
            std::fprintf(stderr, "hdtrans: cannot write stage file %s\n", path.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace hdTrans::cli::stagerecords
