#pragma once
// An immutable dictionary of UTF-8 keys, each with its records in order.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/dict/codes.h"
#include "hdTrans/dict/trie.h"

namespace hdTrans::dict {

// One sense of a key.
struct Record {
    std::string_view target;  // Korean, as written (stem markers included)
    const Codes* codes = nullptr;
    RuleNumber rule = 0;
    int32_t id = 0;  // the number of the id column
};

// Holds each distinct tuple of codes once, at a stable address, numbered in the
// order of their first Intern.
class CodeTable {
public:
    CodeTable() = default;
    CodeTable(CodeTable&&) = default;
    CodeTable& operator=(CodeTable&&) = default;

    // The number of the tuple, added when new.
    int32_t Intern(std::string_view part, std::string_view sem, std::string_view gram, std::string_view dict);
    const Codes* At(int32_t i) const { return &list_[static_cast<std::size_t>(i)]; }
    std::size_t Size() const { return list_.size(); }

    // Set Codes::withXXX and Codes::asPlain of tuple i, adding the tuples they point to.
    void SetWithXXX(int32_t i);
    void SetAsPlain(int32_t i);

    std::size_t MemoryBytes() const;

private:
    void Grow();

    std::deque<Codes> list_;
    std::vector<int32_t> slots_;  // open addressing over list_; -1 for empty
};

class Dict {
public:
    Dict() = default;

    // The number of key or -1.
    int Find(std::string_view key) const { return keys_.Find(key); }
    // Calls f(int key, std::size_t length) for every key that is a prefix of s,
    // shortest first.
    template <class F>
    void Prefixes(std::string_view s, F&& f) const {
        keys_.Prefixes(s, f);
    }
    // Calls f(const Record&) for each record of key number k in order; stops when f
    // returns false.
    template <class F>
    void EachRecord(int k, F&& f) const;
    void AppendRecords(std::vector<Record>& out, int k) const;
    // The records of key, empty for none.
    std::vector<Record> Records(std::string_view key) const;

    // The number of keys.
    std::size_t Len() const { return keys_.KeyCount(); }
    // Calls f(std::string_view key, std::vector<Record> records) for every key in
    // code-point order.
    template <class F>
    void Each(F&& f) const;

    std::size_t MemoryBytes() const;

private:
    friend struct DictBuilder;
    static constexpr int kKeysPerStart = 8;

    // Decodes the record at offset i of the blob; sets i to the offset after it and
    // returns whether another record of the key follows.
    bool Decode(std::size_t& i, Record& r) const;
    std::size_t RecordsStart(int k) const;

    // blob_ holds the records of every key, key number k after key k-1. A record is
    // uvarint(len(target)<<2 | hasRule<<1 | more), target, uvarint(code),
    // uvarint(rule) when rule is not 0, uvarint(id); code is an index in codes_ and
    // more is set when another record of the key follows. starts_[g] is the offset
    // of key number g*kKeysPerStart.
    Trie keys_;
    std::string blob_;
    std::vector<uint32_t> starts_;
    std::vector<const Codes*> codes_;  // by descending use
};

template <class F>
void Dict::EachRecord(int k, F&& f) const {
    std::size_t i = RecordsStart(k);
    for (;;) {
        Record r;
        const bool more = Decode(i, r);
        if (!f(static_cast<const Record&>(r)) || !more) return;
    }
}

template <class F>
void Dict::Each(F&& f) const {
    keys_.Each([&](std::string_view key, int k) {
        std::vector<Record> records;
        AppendRecords(records, k);
        f(key, records);
    });
}

}  // namespace hdTrans::dict
