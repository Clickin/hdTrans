#pragma once
// Trie of UTF-8 keys in LOUDS form (level-order unary degree sequence).
//
// Nodes are numbered breadth first from the root 0, the children of a node in
// code-point order. An edge is one UTF-16 code unit, so a code point above U+FFFF
// takes two edges.
//
//   - tree_ has, for every node in order, a 1 bit per child and then a 0 bit; bit i
//     is tree_[i/64] >> (i%64) & 1. 0 bits are numbered from 0. The children of
//     node n are the count nodes from first = start-n+1, where start is 0 for the
//     root and else the position after 0 bit n-1, and count is the number of 1 bits
//     from start.
//   - zeros_[j] is the position of 0 bit kZeroSample*j.
//   - label_[n] is the edge into node n as UnitLabel of the code unit, which sorts
//     in code-point order; label_[0] is unused.
//   - terminal_ bit n is set when a key ends at node n. That key's number is the
//     number of terminal bits before n; rank_[w] counts those in terminal_[0, w).
//
// Key numbers follow the breadth-first order of the key ends, not the sorted order
// of the keys; Build reports which key has which number. Lookups decode the text as
// text::DecodeRune does (an invalid byte is U+FFFD and advances one byte) and do not
// allocate. A default-constructed Trie has no keys.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "hdTrans/dict/runes.h"

namespace hdTrans::dict {

class Trie {
public:
    // Returns key i of the keys a Trie is built from.
    using KeyFunc = std::string_view (*)(const void* context, std::size_t i);

    // Builds the trie of the keys key(context, 0) ... key(context, count-1), which
    // must be valid UTF-8, sorted and unique. When order is not null, (*order)[k] is
    // set to the index of the key numbered k.
    static Trie Build(std::size_t count, KeyFunc key, const void* context, std::vector<int32_t>* order);
    static Trie Build(std::span<const std::string_view> keys, std::vector<int32_t>* order);

    std::size_t KeyCount() const { return keys_; }
    std::size_t NodeCount() const { return label_.size(); }
    // Bytes held by the arrays of the trie.
    std::size_t MemoryBytes() const;

    // The number of key s, or -1.
    int Find(std::string_view s) const;

    // Calls f(int key, std::size_t length) for every key that is a prefix of s, with
    // its length in bytes, shortest first.
    template <class F>
    void Prefixes(std::string_view s, F&& f) const;

    struct Match {
        int key;             // -1 when no key is a prefix
        std::size_t length;  // bytes
    };
    // The longest key that is a prefix of s.
    Match LongestPrefix(std::string_view s) const;

    // Calls f(std::string_view key, int number) for every key in code-point order.
    template <class F>
    void Each(F&& f) const;

    // The node reached from node by the code point cp, or -1.
    int Next(int node, char32_t cp) const;
    // The number of the key that ends at node, or -1.
    int KeyAt(int node) const;

private:
    static constexpr int kZeroSample = 16;

    static uint16_t UnitLabel(uint16_t u);
    static uint16_t LabelUnit(uint16_t l);

    bool Bit(std::size_t i) const { return (tree_[i / 64] >> (i % 64)) & 1; }
    std::size_t Select0(std::size_t k) const;
    // The position of the first child bit of node n.
    std::size_t ChildStart(int n) const { return n > 0 ? Select0(static_cast<std::size_t>(n - 1)) + 1 : 0; }
    int Child(int n, uint16_t label) const;

    std::vector<uint64_t> tree_;
    std::vector<uint32_t> zeros_;
    std::vector<uint16_t> label_;
    std::vector<uint64_t> terminal_;
    std::vector<uint32_t> rank_;
    std::size_t keys_ = 0;
};

template <class F>
void Trie::Prefixes(std::string_view s, F&& f) const {
    if (label_.empty()) return;
    int n = 0;
    for (std::size_t i = 0;;) {
        if (const int k = KeyAt(n); k >= 0) f(k, i);
        if (i == s.size()) return;
        const auto r = internal::DecodeRuneAt(s.substr(i));
        if ((n = Next(n, r.cp)) < 0) return;
        i += r.size;
    }
}

template <class F>
void Trie::Each(F&& f) const {
    if (label_.empty()) return;
    struct Frame {
        int node;
        std::size_t bit;       // the next child bit
        std::size_t pathSize;  // bytes of the path to node
        uint16_t high;         // the high surrogate of the edge into node, 0 for none
    };
    std::string path;
    std::vector<Frame> stack;
    if (const int k = KeyAt(0); k >= 0) f(std::string_view(), k);
    stack.push_back({0, 0, 0, 0});
    while (!stack.empty()) {
        Frame& top = stack.back();
        if (!Bit(top.bit)) {
            stack.pop_back();
            continue;
        }
        const int c = static_cast<int>(top.bit) - top.node + 1;
        top.bit++;
        const uint16_t unit = LabelUnit(label_[static_cast<std::size_t>(c)]);
        const uint16_t parentHigh = top.high;
        path.resize(top.pathSize);
        uint16_t high = 0;
        if (parentHigh != 0) {
            text::AppendRune(path, 0x10000 + ((char32_t{parentHigh} - 0xD800) << 10) + (char32_t{unit} - 0xDC00));
        } else if (unit >= 0xD800 && unit < 0xDC00) {
            high = unit;
        } else {
            text::AppendRune(path, unit);
        }
        if (high == 0) {
            if (const int k = KeyAt(c); k >= 0) f(std::string_view(path), k);
        }
        stack.push_back({c, ChildStart(c), path.size(), high});
    }
}

}  // namespace hdTrans::dict
