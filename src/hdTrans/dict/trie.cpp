#include "hdTrans/dict/trie.h"

#include <algorithm>
#include <bit>

namespace hdTrans::dict {

namespace {

bool RuneStart(char b) { return (static_cast<unsigned char>(b) & 0xC0) != 0x80; }

uint16_t HighSurrogate(char32_t r) { return static_cast<uint16_t>(0xD800 + ((r - 0x10000) >> 10)); }
uint16_t LowSurrogate(char32_t r) { return static_cast<uint16_t>(0xDC00 + ((r - 0x10000) & 0x3FF)); }

// One UTF-16 code unit of a key: its code unit at byte depth, the low surrogate of
// the code point there when half. next and nextHalf locate the unit after it.
struct Unit {
    uint16_t unit;
    std::size_t next;
    bool nextHalf;
};

Unit UnitAt(std::string_view key, std::size_t depth, bool half) {
    const auto r = text::DecodeRune(key.substr(depth));
    if (r.cp < 0x10000) return {static_cast<uint16_t>(r.cp), depth + r.size, false};
    if (!half) return {HighSurrogate(r.cp), depth, true};
    return {LowSurrogate(r.cp), depth + r.size, false};
}

// The prefix key k shares with the previous key prev: its length in bytes, its
// number of code units and whether it ends inside a code point (the two keys
// share the high surrogate of the code point at that byte).
struct Shared {
    std::size_t at;
    int32_t units;
    bool half;
};

Shared SharedPrefix(std::string_view prev, std::string_view k) {
    std::size_t at = 0;
    while (at < prev.size() && at < k.size() && prev[at] == k[at]) at++;
    while (at > 0 && at < k.size() && !RuneStart(k[at])) at--;
    int32_t units = 0;
    for (std::size_t i = 0; i < at; i++) {
        const auto b = static_cast<unsigned char>(k[i]);
        if (RuneStart(k[i])) units++;
        if (b >= 0xF0) units++;  // a 4-byte code point takes two units
    }
    bool half = false;
    if (at < prev.size() && at < k.size()) {
        const char32_t a = text::DecodeRune(prev.substr(at)).cp;
        const char32_t b = text::DecodeRune(k.substr(at)).cp;
        if (a >= 0x10000 && b >= 0x10000 && HighSurrogate(a) == HighSurrogate(b)) {
            units++;
            half = true;
        }
    }
    return {at, units, half};
}

std::string_view SpanKey(const void* context, std::size_t i) {
    return static_cast<const std::string_view*>(context)[i];
}

}  // namespace

uint16_t Trie::UnitLabel(uint16_t u) {
    if (u < 0xD800) return u;
    if (u >= 0xE000) return static_cast<uint16_t>(u - 0x800);
    return static_cast<uint16_t>(u + 0x2000);
}

uint16_t Trie::LabelUnit(uint16_t l) {
    if (l < 0xD800) return l;
    if (l < 0xF800) return static_cast<uint16_t>(l + 0x800);
    return static_cast<uint16_t>(l - 0x2000);
}

// The nodes of a level are created left to right by the keys in order, so a first
// pass counts the nodes and key ends of each level and a second one writes each
// node at its place: the 1 bit of node c > 0 is bit c-1+parent(c).
Trie Trie::Build(std::size_t count, KeyFunc key, const void* context, std::vector<int32_t>* order) {
    std::vector<int32_t> width{1};  // nodes of each level; the root is level 0
    std::vector<int32_t> ends{0};   // keys ending at each level
    std::string_view prev;
    for (std::size_t i = 0; i < count; i++) {
        const std::string_view k = key(context, i);
        Shared sh = SharedPrefix(prev, k);
        std::size_t at = sh.at;
        std::size_t d = static_cast<std::size_t>(sh.units);
        bool half = sh.half;
        while (at < k.size()) {
            const Unit u = UnitAt(k, at, half);
            at = u.next;
            half = u.nextHalf;
            if (++d == width.size()) {
                width.push_back(0);
                ends.push_back(0);
            }
            width[d]++;
        }
        ends[d]++;
        prev = k;
    }

    // next[d] is the number of the next node of level d, cur[d] that of the last
    // one; endAt[d] the next key number of a key ending at level d.
    const std::size_t levels = width.size();
    std::vector<int32_t> next(levels), cur(levels), endAt(levels);
    int32_t nodes = 0;
    int32_t keys = 0;
    for (std::size_t d = 0; d < levels; d++) {
        next[d] = nodes;
        endAt[d] = keys;
        nodes += width[d];
        keys += ends[d];
    }
    Trie t;
    t.keys_ = count;
    const auto n64 = static_cast<std::size_t>(nodes);
    t.tree_.assign((2 * n64 + 63) / 64, 0);
    t.label_.assign(n64, 0);
    t.terminal_.assign(n64 / 64 + 1, 0);
    next[0] = 1;
    if (order) order->assign(count, 0);
    prev = {};
    for (std::size_t i = 0; i < count; i++) {
        const std::string_view k = key(context, i);
        Shared sh = SharedPrefix(prev, k);
        std::size_t at = sh.at;
        std::size_t d = static_cast<std::size_t>(sh.units);
        bool half = sh.half;
        while (at < k.size()) {
            const Unit u = UnitAt(k, at, half);
            at = u.next;
            half = u.nextHalf;
            d++;
            const int32_t c = next[d]++;
            t.label_[static_cast<std::size_t>(c)] = UnitLabel(u.unit);
            const auto b = static_cast<std::size_t>(c - 1 + cur[d - 1]);
            t.tree_[b / 64] |= uint64_t{1} << (b % 64);
            cur[d] = c;
        }
        const auto n = static_cast<std::size_t>(cur[d]);
        t.terminal_[n / 64] |= uint64_t{1} << (n % 64);
        const int32_t number = endAt[d]++;
        if (order) (*order)[static_cast<std::size_t>(number)] = static_cast<int32_t>(i);
        prev = k;
    }

    const std::size_t bits = 2 * n64 - 1;
    t.zeros_.reserve((n64 + kZeroSample - 1) / kZeroSample);
    std::size_t zero = 0;
    for (std::size_t i = 0; i < bits; i++) {
        if (!t.Bit(i)) {
            if (zero % kZeroSample == 0) t.zeros_.push_back(static_cast<uint32_t>(i));
            zero++;
        }
    }
    t.rank_.assign(t.terminal_.size(), 0);
    for (std::size_t w = 1; w < t.terminal_.size(); w++) {
        t.rank_[w] = t.rank_[w - 1] + static_cast<uint32_t>(std::popcount(t.terminal_[w - 1]));
    }
    return t;
}

Trie Trie::Build(std::span<const std::string_view> keys, std::vector<int32_t>* order) {
    return Build(keys.size(), SpanKey, keys.data(), order);
}

std::size_t Trie::MemoryBytes() const {
    return tree_.size() * sizeof(tree_[0]) + zeros_.size() * sizeof(zeros_[0]) + label_.size() * sizeof(label_[0]) +
           terminal_.size() * sizeof(terminal_[0]) + rank_.size() * sizeof(rank_[0]);
}

std::size_t Trie::Select0(std::size_t k) const {
    const std::size_t p = zeros_[k / kZeroSample];
    std::size_t w = p / 64;
    uint64_t x = ~tree_[w] & ~((uint64_t{1} << (p % 64)) - 1);
    k %= kZeroSample;
    for (auto c = static_cast<std::size_t>(std::popcount(x)); k >= c; c = static_cast<std::size_t>(std::popcount(x))) {
        k -= c;
        x = ~tree_[++w];
    }
    for (; k > 0; k--) x &= x - 1;
    return w * 64 + static_cast<std::size_t>(std::countr_zero(x));
}

int Trie::Child(int n, uint16_t label) const {
    const std::size_t start = ChildStart(n);
    // The 0 bit ending the children is usually in the word of start.
    std::size_t end = start;
    if (const uint64_t x = ~tree_[start / 64] >> (start % 64); x != 0) {
        end += static_cast<std::size_t>(std::countr_zero(x));
    } else {
        end = Select0(static_cast<std::size_t>(n));
    }
    const std::size_t first = start - static_cast<std::size_t>(n) + 1;
    const uint16_t* begin = label_.data() + first;
    const uint16_t* stop = begin + (end - start);
    const uint16_t* it = std::lower_bound(begin, stop, label);
    if (it == stop || *it != label) return -1;
    return static_cast<int>(it - label_.data());
}

int Trie::Next(int node, char32_t cp) const {
    if (label_.empty()) return -1;
    if (cp < 0x10000) return Child(node, UnitLabel(static_cast<uint16_t>(cp)));
    if ((node = Child(node, UnitLabel(HighSurrogate(cp)))) < 0) return -1;
    return Child(node, UnitLabel(LowSurrogate(cp)));
}

int Trie::KeyAt(int node) const {
    if (label_.empty()) return -1;
    const auto n = static_cast<std::size_t>(node);
    const uint64_t x = terminal_[n / 64];
    const uint64_t bit = uint64_t{1} << (n % 64);
    if ((x & bit) == 0) return -1;
    return static_cast<int>(rank_[n / 64]) + std::popcount(x & (bit - 1));
}

int Trie::Find(std::string_view s) const {
    if (label_.empty()) return -1;
    int n = 0;
    while (!s.empty()) {
        const auto r = text::DecodeRune(s);
        if ((n = Next(n, r.cp)) < 0) return -1;
        s.remove_prefix(r.size);
    }
    return KeyAt(n);
}

Trie::Match Trie::LongestPrefix(std::string_view s) const {
    Match m{-1, 0};
    Prefixes(s, [&m](int k, std::size_t length) { m = {k, length}; });
    return m;
}

}  // namespace hdTrans::dict
