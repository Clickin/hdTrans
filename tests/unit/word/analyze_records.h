#pragma once
// The analyze fixtures: lookup environments, the chunks of a set and the LK, LKC, CM,
// L, LC and LD records the analyze stage writes for a chunk.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hdTrans/dict/snapshot.h"
#include "hdTrans/tokenizer/token.h"
#include "hdTrans/word/analyze.h"
#include "hdTrans/word/worker.h"
#include "fnv.h"
#include "tokenizer/stage_records.h"

namespace analyzetest {

using namespace hdTrans;

inline bool ReadFile(const std::string& path, std::string& text) { return stagetest::ReadFile(path, text); }

inline int Int(std::string_view s) { return std::atoi(std::string(s).c_str()); }

// The tag of a record line.
inline std::string_view Tag(std::string_view line) { return line.substr(0, line.find('\t')); }

// A compiled plain-entry list (entries/<n>.tsv): key target pos sem gram prio file line
// order flags negative, text columns escaped.
struct EntryList {
    dict::CodeTable codes;
    std::unique_ptr<dict::UserText> text;
};

inline std::unique_ptr<EntryList> ReadEntries(const std::string& path) {
    std::string data;
    if (!ReadFile(path, data)) return nullptr;
    auto out = std::make_unique<EntryList>();
    std::vector<dict::UserEntry> list;
    for (const std::string_view line : stagetest::Lines(data)) {
        if (line.empty()) continue;
        const auto f = stagetest::Columns(line);
        if (f.size() != 11) return nullptr;
        dict::UserEntry e;
        e.key = stagetest::UnescapeStageText(f[0]);
        e.target = stagetest::UnescapeStageText(f[1]);
        e.codes = out->codes.At(out->codes.Intern(stagetest::UnescapeStageText(f[2]), stagetest::UnescapeStageText(f[3]),
                                                  stagetest::UnescapeStageText(f[4]), "GN"));
        e.prio = Int(f[5]);
        e.file = Int(f[6]);
        e.line = Int(f[7]);
        e.order = Int(f[8]);
        std::string reg;
        for (const char c : f[9]) {
            switch (c) {
            case 'N': e.name = true; break;
            case 'L': e.nameLayer = true; break;
            case 'K': e.kana = true; break;
            case 'F': e.force = true; break;
            case 'H': e.hidden = true; break;
            case 'O': e.honorific = true; break;
            case 'E': e.end = true; break;
            case 'P': e.polite = true; break;
            case 'S': e.stative = true; break;
            case 'v': case 'c': case 'l': e.finalClass = c; break;
            default: reg += c;
            }
        }
        if (!reg.empty()) e.registerLevel = static_cast<uint8_t>(std::atoi(reg.c_str()));
        e.negative = stagetest::UnescapeStageText(f[10]);
        list.push_back(std::move(e));
    }
    out->text = std::make_unique<dict::UserText>(std::move(list));
    return out;
}

// One lookup environment of env.tsv: id entries strategy prior text terminal
// hanjaToHangul deleteJapan register0 register1 nConfirmed keys... nPersons keys...
struct Environment {
    int entries = -1;
    dict::Strategy strategy = dict::Strategy::LongestMatch;
    std::string prior;
    word::Settings settings;
    dict::KeySet confirmed, persons;
    dict::LookupContext lookup;
};

class Environments {
public:
    explicit Environments(std::string dir) : dir_(std::move(dir)) {
        std::string data;
        ReadFile(dir_ + "env.tsv", data);
        for (const std::string_view line : stagetest::Lines(data)) {
            const auto f = stagetest::Columns(line);
            if (f.size() < 12) continue;
            Environment& e = envs_[Int(f[0])];
            e.entries = Int(f[1]);
            e.strategy = Int(f[2]) == 1 ? dict::Strategy::UserFirst : dict::Strategy::LongestMatch;
            e.prior = stagetest::UnescapeStageText(f[3]);
            e.settings.text = f[4] == "1";
            e.settings.hanjaToHangul = Int(f[6]);
            e.settings.deleteJapan = Int(f[7]);
            e.settings.registers[0] = static_cast<uint8_t>(Int(f[8]));
            e.settings.registers[1] = static_cast<uint8_t>(Int(f[9]));
            std::size_t k = 10;
            for (int n = Int(f[k++]); n > 0 && k < f.size(); n--) e.confirmed.insert(stagetest::UnescapeStageText(f[k++]));
            if (k < f.size()) {
                for (int n = Int(f[k++]); n > 0 && k < f.size(); n--) e.persons.insert(stagetest::UnescapeStageText(f[k++]));
            }
        }
    }

    // The environment with its plain entries loaded; null when unknown.
    const Environment* Get(int id) {
        const auto it = envs_.find(id);
        if (it == envs_.end()) return nullptr;
        Environment& e = it->second;
        const EntryList* list = Entries(e.entries);
        if (list == nullptr) return nullptr;
        e.lookup = {};
        e.lookup.plain = list->text.get();
        e.lookup.strategy = e.strategy;
        e.lookup.priorDict = e.prior;
        e.lookup.confirmed = e.confirmed.empty() ? nullptr : &e.confirmed;
        e.lookup.persons = e.persons.empty() ? nullptr : &e.persons;
        return &e;
    }

private:
    // The most recently used lists are kept.
    const EntryList* Entries(int id) {
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->first == id) {
                cache_.splice(cache_.begin(), cache_, it);
                return cache_.front().second.get();
            }
        }
        auto list = ReadEntries(dir_ + "entries/" + std::to_string(id) + ".tsv");
        if (!list) return nullptr;
        cache_.emplace_front(id, std::move(list));
        if (cache_.size() > 4) cache_.pop_back();
        return cache_.front().second.get();
    }

    std::string dir_;
    std::map<int, Environment> envs_;
    std::list<std::pair<int, std::unique_ptr<EntryList>>> cache_;
};

// One chunk of a set: its pieces, environment and expected records.
struct Chunk {
    bool active = false;
    std::string header;
    int index = 0;
    int env = -1;
    int faultSentence = -1;
    std::vector<tokenizer::Piece> pieces;
    std::vector<std::string> want;
};

// Calls finish for every chunk of a fixture file, with chunk filled.
template <class Finish>
void ForEachChunk(std::string_view data, Chunk& chunk, Finish finish) {
    std::string header;
    std::vector<tokenizer::Piece> pending;
    int pendingIndex = -1;
    bool fromLine = false;
    for (const std::string_view line : stagetest::Lines(data)) {
        if (line.starts_with("#")) {
            finish();
            if (line.starts_with("#CASE")) header = std::string(line);
            else header = header.substr(0, header.find('|')) + "|" + std::string(line);
            pending.clear();
            pendingIndex = -1;
            fromLine = false;
            continue;
        }
        const auto cols = stagetest::Columns(line);
        const std::string_view tag = cols[0];
        if (tag == "IN") {
            // The raw path translates the line as one piece; the text path its CH pieces.
            finish();
            pending.clear();
            const std::string text = stagetest::UnescapeStageText(cols[1]);
            if (!text.empty()) pending.push_back({text, 0});
            pendingIndex = 0;
            fromLine = true;
        } else if (tag == "CH") {
            const int k = Int(cols[1]);
            if (fromLine) {
                pending.clear();
                pendingIndex = -1;
                fromLine = false;
            }
            if (chunk.active || k != pendingIndex) {
                finish();
                if (k != pendingIndex) pending.clear();
            }
            pendingIndex = k;
            pending.push_back({stagetest::UnescapeStageText(cols[4]), Int(cols[3])});
        } else if (tag == "ENV") {
            finish();
            chunk.active = true;
            chunk.header = header;
            chunk.index = pendingIndex < 0 ? 0 : pendingIndex;
            chunk.env = Int(cols[1]);
            chunk.faultSentence = -1;
            chunk.pieces = pending;
            chunk.want.clear();
        } else if (tag == "FAULT") {
            if (chunk.active) chunk.faultSentence = Int(cols[2]);
        } else if (chunk.active && (tag == "LK" || tag == "LKC" || tag == "CM" || tag == "L" || tag == "LC" || tag == "LD" || tag == "LX")) {
            chunk.want.emplace_back(line);
        }
    }
    finish();
}

inline std::string Esc(std::string_view s) { return stagetest::EscapeStageText(s); }

inline std::string PartCode(const word::Entry& e) {
    if (e.cls == 0) return std::string(e.code);
    return std::string(1, e.cls) + std::string(e.code);
}

struct Lookups {
    int chunk = 0;
    struct One {
        int token;
        std::vector<std::string> lines;
    };
    std::vector<One> list;
};

inline void OnLookup(void* context, int token, std::string_view query, int mode, const word::Record* records) {
    Lookups& l = *static_cast<Lookups*>(context);
    Lookups::One& one = l.list.emplace_back();
    one.token = token;
    int n = 0;
    for (const word::Record* r = records; r != nullptr; r = r->child) n++;
    one.lines.push_back("LK\t" + std::to_string(l.chunk) + "\t" + std::to_string(token) + "\t" + std::to_string(mode) + "\t" +
                        Esc(query) + "\t" + std::to_string(n));
    n = 0;
    for (const word::Record* r = records; r != nullptr; r = r->child) {
        one.lines.push_back("LKC\t" + std::to_string(n++) + "\t" + Esc(r->partCode) + "\t" + Esc(r->source) + "\t" +
                            Esc(r->target) + "\t" + Esc(r->semantic) + "\t" + Esc(r->attr) + "\t" + Esc(r->dictCode) + "\t" +
                            std::to_string(r->ruleNo) + "\t" + (r->user != nullptr ? "1" : "0"));
    }
}

// FNV-1a over the bytes of s, appended to h.
inline void HashText(hdtest::Fnv& h, std::string_view s) {
    for (const char c : s) h.Byte(static_cast<uint8_t>(c));
}

inline std::string Hex16(uint64_t v) {
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

// The records of a list from r on: their count and the digest of their fields.
inline std::pair<int, uint64_t> RecordsDigest(const word::Record* r) {
    hdtest::Fnv h;
    int n = 0;
    for (; r != nullptr; r = r->child) {
        n++;
        HashText(h, r->source);
        h.Byte(0x1f);
        HashText(h, r->target);
        h.Byte(0x1f);
        HashText(h, r->partCode);
        h.Byte(0x1f);
        HashText(h, r->semantic);
        h.Byte(0x1f);
        HashText(h, r->attr);
        h.Byte(0x1f);
        HashText(h, r->dictCode);
        h.Byte(0x1f);
        HashText(h, std::to_string(r->ruleNo));
        h.Byte(0x1f);
        HashText(h, r->user != nullptr ? "1" : "0");
        h.Byte(0x1e);
    }
    return {n, h.h};
}

// The plain-entry digest of the candidates of an item: the user fields of every entry
// ("-" for a base entry).
inline uint64_t UsersDigest(const std::vector<word::Entry*>& entries) {
    hdtest::Fnv h;
    for (const word::Entry* e : entries) {
        const dict::UserEntry* u = e->user;
        if (u == nullptr) {
            HashText(h, "-");
            h.Byte(0x1e);
            continue;
        }
        const std::string fields[] = {u->key, u->target, std::string(u->Part().Text()), std::to_string(u->registerLevel),
                                      u->polite ? "1" : "0", u->stative ? "1" : "0", u->end ? "1" : "0", u->negative,
                                      u->kana ? "1" : "0", u->name ? "1" : "0"};
        for (std::size_t i = 0; i < std::size(fields); i++) {
            if (i > 0) h.Byte(0x1f);
            HashText(h, fields[i]);
        }
        h.Byte(0x1e);
    }
    return h.h;
}

// The LX record of an item: the analyze state the L, LC and LD records leave out
// (detail classes, punctuation, voice, pattern, candidate records, plain-entry digest).
inline std::string ItemState(int chunk, int sent, const word::Item& it) {
    const word::Detail& d = *it.detail;
    const word::Group* g = it.group;
    std::string classes;
    for (int i = 0; i < it.DirectCount(); i++) {
        if (i > 0) classes += ',';
        classes += std::to_string(d.classes[i]);
    }
    const auto [restN, restH] = RecordsDigest(g->records);
    int dn = 0, dlen = 0;
    uint64_t dh = hdtest::Fnv{}.h;
    std::vector<word::Entry*> entries = g->entries;
    if (const word::Group* dg = it.DirectGroup(); dg != nullptr) {
        const auto [n, h] = RecordsDigest(dg->records);
        dn = n;
        dh = h;
        dlen = dg->length;
        entries.insert(entries.end(), dg->entries.begin(), dg->entries.end());
    }
    hdtest::Fnv text;
    HashText(text, it.text);
    const std::string cols[] = {
        "LX", std::to_string(chunk), std::to_string(sent), std::to_string(it.tokenIndex) + ":" + std::to_string(it.start),
        Hex16(text.h), std::to_string(it.start), std::to_string(it.length), std::to_string(g->length),
        std::to_string(d.nextType), Esc(d.nextText), std::to_string(d.kind), std::to_string(d.record),
        std::to_string(d.punct), std::to_string(d.resultCount), std::to_string(d.selected), std::to_string(d.scanOffset),
        std::to_string(d.patternKind), std::to_string(d.patternMatched), std::to_string(d.patternLength), classes,
        std::to_string(restN), Hex16(restH), std::to_string(dn), Hex16(dh), std::to_string(dlen),
        Hex16(UsersDigest(entries))};
    std::string out;
    for (std::size_t i = 0; i < std::size(cols); i++) {
        if (i > 0) out += '\t';
        out += cols[i];
    }
    return out;
}

// The records of the sentence's chain before its sentence passes.
inline void SentenceRecords(const tokenizer::Tokens& tokens, const word::Sentence& s, int chunk, int sent,
                            std::vector<std::string>& out) {
    static const char* const kDetailNames[] = {"TWordInfo", "TVerbWord", "TKeiyoWord", "TDakeiyoWord"};
    const std::string c = std::to_string(chunk), n = std::to_string(sent);
    for (int k = s.markerBegin; k < s.markerEnd; k++) {
        const tokenizer::ControlMarker& m = tokens.markers[static_cast<std::size_t>(k)];
        out.push_back("CM\t" + c + "\t" + n + "\t" + std::to_string(m.tokenIndex) + "\t" + std::to_string(m.textLength) + "\t" +
                      std::to_string(m.markerKind));
    }
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
    for (const word::Item* it = s.chain.head; it != nullptr; it = it->next) {
        out.push_back(ItemState(chunk, sent, *it));
    }
}

// Runs the analyze stage over one chunk and writes its records in the order of the
// reference: the lookups of each sentence, then its chain. With faultSentence >= 0 the
// chunk ended in that sentence's passes: nothing after its chain is written.
inline void Analyze(const tokenizer::Tokens& tokens, const dict::Snapshot& snapshot, const Environment& env, int chunk,
                    int faultSentence, word::Arena& arena, word::Analysis& analysis, std::vector<std::string>& out) {
    Lookups lookups;
    lookups.chunk = chunk;
    word::Analyze(tokens, snapshot, env.lookup, env.settings, arena, analysis, {OnLookup, &lookups});
    std::size_t next = 0;
    for (std::size_t s = 0; s < analysis.sentences.size(); s++) {
        const word::Sentence& sentence = analysis.sentences[s];
        while (next < lookups.list.size() && lookups.list[next].token <= sentence.last) {
            for (std::string& l : lookups.list[next++].lines) out.push_back(std::move(l));
        }
        SentenceRecords(tokens, sentence, chunk, static_cast<int>(s), out);
        if (static_cast<int>(s) == faultSentence) return;
    }
    while (next < lookups.list.size()) {
        for (std::string& l : lookups.list[next++].lines) out.push_back(std::move(l));
    }
}

}  // namespace analyzetest
