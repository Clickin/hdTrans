#include <deque>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "hdTrans/word/worker.h"
#include "hdtest.h"

using namespace hdTrans;

namespace {

// Records with the given (source, part code) pairs in order, kept alive by pool.
word::Record* RecordList(std::deque<word::Record>& pool,
                         std::initializer_list<std::pair<std::string_view, std::string_view>> list) {
    word::Record* head = nullptr;
    word::Record* last = nullptr;
    for (const auto& [source, partCode] : list) {
        word::Record& r = pool.emplace_back();
        r.source = source;
        r.partCode = partCode;
        if (last != nullptr) last->child = &r;
        else head = &r;
        last = &r;
    }
    return head;
}

}  // namespace

// Branch and length of the conjugation, or no match.
HD_TEST(verb_analyse_tail) {
    struct Case {
        std::string_view key;
        char kind;
        std::string_view tail;
        bool ok;
        int branch, length;
    };
    const Case cases[] = {
        {"書", '1', "いた", true, 6, 6},      {"書", '1', "いて", true, 7, 6},
        {"書", '1', "かない", true, 8, 3},    {"書", '1', "く", true, 0, 3},
        {"書", '1', "いたら", true, 6, 6},    {"書", '1', "いた、", true, 6, 6},
        {"書", '1', "いたっけ", true, 6, 6},  {"行", '?', "った", true, 6, 6},
        {"行", '?', "く", true, 0, 3},        {"行", '1', "った", false, 0, 0},
        {"食べ", '3', "た", false, 0, 0},     {"食べ", '3', "て", false, 0, 0},
        {"食べ", '3', "ない", false, 0, 0},   {"食べ", '3', "", false, 0, 0},
        {"見", '2', "た", false, 0, 0},       {"書", '1', "ぃた", false, 0, 0},
        {"書", '1', "", false, 0, 0},         {"書", '7', "た", true, 6, 3},
        {"書", '7', "いた", true, 8, 0},      {"する", '5', "た", false, 0, 0},
    };
    for (const Case& c : cases) {
        int branch = 0, length = 0;
        const bool ok = word::AnalyseVerbTail(c.key, c.kind, c.tail, branch, length);
        HD_CHECK_EQ(ok, c.ok);
        if (c.ok) {
            HD_CHECK_EQ(branch, c.branch);
            HD_CHECK_EQ(length, c.length);
        }
    }
}

// The branch, the pattern kind of the last record tried, and the record the walk stopped at
// (null when it ran out).
HD_TEST(verb_match_conjugation) {
    struct Case {
        std::string_view text;
        int offset;
        std::initializer_list<std::pair<std::string_view, std::string_view>> records;
        bool ok;
        int kind, patternKind;
        std::string_view stop;  // part code of the stop record, "" for none
    };
    const Case cases[] = {
        {"書いた", 0, {{"書", "M421"}}, true, 6, 1, "M421"},
        {"書いた", 1, {{"いた", "M421"}}, false, -1, 1, ""},
        {"書かない", 0, {{"書", "M422"}, {"書", "M425"}}, false, -1, 5, ""},
        {"書いた", 0, {{"書", "M122"}, {"書", "M421"}}, false, -1, -1, "M122"},
        {"食べた", 0, {{"食べ", "M423"}}, false, -1, 3, ""},
        {"食べた", 0, {{"食べ", "M431"}, {"食べ", "M433"}}, false, -1, 3, ""},
        {"書いた", 9, {{"書", "M421"}}, false, -1, -1, "M421"},
    };
    for (const Case& c : cases) {
        std::deque<word::Record> pool;
        word::Record* records = RecordList(pool, c.records);
        word::Detail d;
        word::Record* stop = nullptr;
        const bool ok = word::MatchConjugation(d, c.text, c.offset, records, stop);
        HD_CHECK_EQ(ok, c.ok);
        HD_CHECK_EQ(d.kind, c.kind);
        HD_CHECK_EQ(d.patternKind, c.patternKind);
        HD_CHECK_EQ(std::string(stop == nullptr ? std::string_view() : stop->partCode), std::string(c.stop));
    }
}

// Part code byte 3 '>' is pattern kind 14 (no ending): it matches with the length of the
// record's source and leaves the branch unset.
HD_TEST(verb_match_direct_kind) {
    std::deque<word::Record> pool;
    word::Record* records = RecordList(pool, {{"書", "M42>"}});
    word::Detail d;
    word::Record* stop = nullptr;
    HD_CHECK(word::MatchConjugation(d, "書", 0, records, stop));
    HD_CHECK_EQ(d.kind, -1);
    HD_CHECK_EQ(d.patternKind, 14);
    HD_CHECK_EQ(d.patternMatched, 1);
    HD_CHECK_EQ(d.patternLength, 3);
}
