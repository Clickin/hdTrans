#pragma once
// Analyze-state records for the small marker tests.
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hdTrans/tokenizer/token.h"
#include "hdTrans/word/analyze.h"
#include "hdTrans/word/worker.h"
#include "fnv.h"
#include "tokenizer/stage_records.h"

namespace analyzetest {

using namespace hdTrans;

inline std::string_view Tag(std::string_view line) { return line.substr(0, line.find('\t')); }

inline std::string Esc(std::string_view s) { return stagetest::EscapeStageText(s); }

inline std::string PartCode(const word::Entry& e) {
    if (e.cls == 0) return std::string(e.code);
    return std::string(1, e.cls) + std::string(e.code);
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

}  // namespace analyzetest
