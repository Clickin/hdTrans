#include "core/dict/trie.h"

#include <cstdint>
#include <algorithm>
#include <bit>
#include <limits>

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
    const auto r = internal::DecodeRuneAt(key.substr(depth));
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
    // Only code points above U+FFFF, with a lead byte F0..F4, share a high surrogate.
    if (at < prev.size() && at < k.size() && static_cast<unsigned char>(prev[at]) >= 0xF0 &&
        static_cast<unsigned char>(k[at]) >= 0xF0) {
        const char32_t a = internal::DecodeRuneAt(prev.substr(at)).cp;
        const char32_t b = internal::DecodeRuneAt(k.substr(at)).cp;
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
    t.storage_ = std::make_unique<Storage>();
    Storage& arrays = *t.storage_;
    t.keys_ = count;
    const auto n64 = static_cast<std::size_t>(nodes);
    arrays.tree.assign((2 * n64 + 63) / 64, 0);
    arrays.label.assign(n64, 0);
    arrays.terminal.assign(n64 / 64 + 1, 0);
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
            arrays.label[static_cast<std::size_t>(c)] = UnitLabel(u.unit);
            const auto b = static_cast<std::size_t>(c - 1 + cur[d - 1]);
            arrays.tree[b / 64] |= uint64_t{1} << (b % 64);
            cur[d] = c;
        }
        const auto n = static_cast<std::size_t>(cur[d]);
        arrays.terminal[n / 64] |= uint64_t{1} << (n % 64);
        const int32_t number = endAt[d]++;
        if (order) (*order)[static_cast<std::size_t>(number)] = static_cast<int32_t>(i);
        prev = k;
    }

    const std::size_t bits = 2 * n64 - 1;
    arrays.zeros.reserve((n64 + kZeroSample - 1) / kZeroSample);
    std::size_t zero = 0;
    for (std::size_t w = 0; w * 64 < bits; w++) {
        uint64_t zeros = ~arrays.tree[w];
        if (bits - w * 64 < 64) zeros &= (uint64_t{1} << (bits - w * 64)) - 1;
        // The next sampled zero is number zero rounded up to kZeroSample.
        for (;;) {
            const std::size_t skip = (kZeroSample - zero % kZeroSample) % kZeroSample;
            const auto zeroCount = static_cast<std::size_t>(std::popcount(zeros));
            if (skip >= zeroCount) {
                zero += zeroCount;
                break;
            }
            for (std::size_t k = 0; k < skip; k++) zeros &= zeros - 1;
            zero += skip;
            arrays.zeros.push_back(static_cast<uint32_t>(w * 64 + static_cast<std::size_t>(std::countr_zero(zeros))));
            zeros &= zeros - 1;
            zero++;
        }
    }
    arrays.rank.assign(arrays.terminal.size(), 0);
    for (std::size_t w = 1; w < arrays.terminal.size(); w++) {
        arrays.rank[w] = arrays.rank[w - 1] + static_cast<uint32_t>(std::popcount(arrays.terminal[w - 1]));
    }
    t.tree_ = arrays.tree;
    t.zeros_ = arrays.zeros;
    t.label_ = arrays.label;
    t.terminal_ = arrays.terminal;
    t.rank_ = arrays.rank;
    return t;
}

Trie Trie::Build(std::span<const std::string_view> keys, std::vector<int32_t>* order) {
    return Build(keys.size(), SpanKey, keys.data(), order);
}

Trie Trie::FromView(ViewData data) {
    Trie t;
    t.tree_ = data.tree;
    t.zeros_ = data.zeros;
    t.label_ = data.label;
    t.terminal_ = data.terminal;
    t.rank_ = data.rank;
    t.keys_ = data.keys;
    t.guard_ = data.guard;
    return t;
}

std::size_t Trie::MemoryBytes() const {
    return tree_.size() * sizeof(tree_[0]) + zeros_.size() * sizeof(zeros_[0]) + label_.size() * sizeof(label_[0]) +
           terminal_.size() * sizeof(terminal_[0]) + rank_.size() * sizeof(rank_[0]);
}

bool Trie::Ready() const {
    if (!guard_.Check()) return guard_.Fail();
    const std::size_t n = label_.size();
    if (n == 0) {
        if (!tree_.empty() || !zeros_.empty() || !terminal_.empty() || !rank_.empty() || keys_ != 0)
            return guard_.Fail();
        return true;
    }
    if (n > static_cast<std::size_t>(std::numeric_limits<int>::max()) || keys_ > n ||
        tree_.size() != n / 32 + (n % 32 != 0) || zeros_.size() != (n + kZeroSample - 1) / kZeroSample ||
        terminal_.size() != n / 64 + 1 || rank_.size() != terminal_.size()) return guard_.Fail();
    return true;
}

bool Trie::Bit(std::size_t i, bool& value) const {
    if (label_.empty() || i >= 2 * label_.size() - 1 || i / 64 >= tree_.size()) return guard_.Fail();
    value = ((tree_[i / 64] >> (i % 64)) & 1) != 0;
    return true;
}

bool Trie::Select0(std::size_t k, std::size_t& position) const {
    if (k >= label_.size() || k / kZeroSample >= zeros_.size()) return guard_.Fail();
    const std::size_t bits = 2 * label_.size() - 1;
    const std::size_t p = zeros_[k / kZeroSample];
    if (p >= bits || p / 64 >= tree_.size()) return guard_.Fail();
    std::size_t w = p / 64;
    const uint64_t word = tree_[w];
    if (((word >> (p % 64)) & 1) != 0) return guard_.Fail();
    uint64_t x = ~word & ~((uint64_t{1} << (p % 64)) - 1);
    k %= kZeroSample;
    for (;;) {
        if (bits - w * 64 < 64) x &= (uint64_t{1} << (bits - w * 64)) - 1;
        const auto count = static_cast<std::size_t>(std::popcount(x));
        if (k < count) break;
        k -= count;
        if (++w >= tree_.size() || w * 64 >= bits) return guard_.Fail();
        x = ~tree_[w];
    }
    for (; k > 0; k--) x &= x - 1;
    position = w * 64 + static_cast<std::size_t>(std::countr_zero(x));
    return true;
}

bool Trie::Children(int node, std::size_t& start, std::size_t& end, std::size_t& first) const {
    if (node < 0 || static_cast<std::size_t>(node) >= label_.size()) return guard_.Fail();
    const auto n = static_cast<std::size_t>(node);
    start = 0;
    if (n > 0) {
        if (!Select0(n - 1, start)) return false;
        start++;
    }
    // Separate bounds and bit access avoid incorrect inlining under MSVC x86 /O2.
    if (start >= 2 * label_.size() - 1 || start / 64 >= tree_.size()) return guard_.Fail();
    const uint64_t x = ~tree_[start / 64] >> (start % 64);
    if (x != 0) end = start + static_cast<std::size_t>(std::countr_zero(x));
    else if (!Select0(n, end)) return false;
    if (end < start || end >= 2 * label_.size() - 1 || start < n) return guard_.Fail();
    first = start - n + 1;
    if (first <= n || first > label_.size() || end - start > label_.size() - first) return guard_.Fail();
    return true;
}

int Trie::Child(int node, uint16_t label) const {
    std::size_t start, end, first;
    if (!Children(node, start, end, first)) return -1;
    const uint16_t* begin = label_.data() + first;
    const uint16_t* stop = begin + (end - start);
    const uint16_t* it = std::lower_bound(begin, stop, label);
    if (it == stop || *it != label) return -1;
    return static_cast<int>(it - label_.data());
}

int Trie::NextNode(int node, char32_t cp) const {
    if (label_.empty()) return -1;
    if (cp > 0x10FFFF) {
        guard_.Fail();
        return -1;
    }
    if (cp < 0x10000) return Child(node, UnitLabel(static_cast<uint16_t>(cp)));
    if ((node = Child(node, UnitLabel(HighSurrogate(cp)))) < 0) return -1;
    return Child(node, UnitLabel(LowSurrogate(cp)));
}

int Trie::Next(int node, char32_t cp) const {
    return Ready() ? NextNode(node, cp) : -1;
}

int Trie::KeyNumber(int node) const {
    if (label_.empty()) return -1;
    if (node < 0 || static_cast<std::size_t>(node) >= label_.size()) {
        guard_.Fail();
        return -2;
    }
    const auto n = static_cast<std::size_t>(node);
    const uint64_t x = terminal_[n / 64];
    const uint64_t bit = uint64_t{1} << (n % 64);
    if ((x & bit) == 0) return -1;
    const auto before = static_cast<std::size_t>(rank_[n / 64]);
    const auto within = static_cast<std::size_t>(std::popcount(x & (bit - 1)));
    if (before >= keys_ || within >= keys_ - before) {
        guard_.Fail();
        return -2;
    }
    return static_cast<int>(before + within);
}

int Trie::KeyAt(int node) const {
    if (!Ready()) return -1;
    const int key = KeyNumber(node);
    return key >= 0 ? key : -1;
}

bool Trie::Validate() const {
    if (!Ready()) return false;
    const std::size_t n = label_.size();
    if (n == 0) return true;
    if (label_[0] != 0) return guard_.Fail();
    const std::size_t bits = 2 * n - 1;
    if ((tree_.back() >> (bits % 64)) != 0) return guard_.Fail();
    std::size_t at = 0, child = 1;
    for (std::size_t node = 0; node < n; node++) {
        if (node >= child) return guard_.Fail();
        const uint16_t parentUnit = LabelUnit(label_[node]);
        const bool high = node != 0 && parentUnit >= 0xD800 && parentUnit < 0xDC00;
        const std::size_t first = child;
        while (at < bits && ((tree_[at / 64] >> (at % 64)) & 1) != 0) {
            if (child >= n || (child > first && label_[child - 1] >= label_[child])) return guard_.Fail();
            const uint16_t unit = LabelUnit(label_[child]);
            if (high != (unit >= 0xDC00 && unit < 0xE000)) return guard_.Fail();
            child++;
            at++;
        }
        if (at >= bits || (node % kZeroSample == 0 && zeros_[node / kZeroSample] != at)) return guard_.Fail();
        if (high && (child == first || ((terminal_[node / 64] >> (node % 64)) & 1) != 0)) return guard_.Fail();
        at++;
    }
    if (at != bits || child != n) return guard_.Fail();
    std::size_t keys = 0;
    for (std::size_t w = 0; w < terminal_.size(); w++) {
        if (rank_[w] != keys) return guard_.Fail();
        const std::size_t remaining = n - w * 64;
        if (remaining < 64 && (terminal_[w] >> remaining) != 0) return guard_.Fail();
        keys += static_cast<std::size_t>(std::popcount(terminal_[w]));
    }
    return keys == keys_ ? true : guard_.Fail();
}

int Trie::Find(std::string_view s) const {
    if (!Ready() || label_.empty()) return -1;
    int n = 0;
    while (!s.empty()) {
        const auto r = internal::DecodeRuneAt(s);
        if ((n = NextNode(n, r.cp)) < 0) return -1;
        s.remove_prefix(r.size);
    }
    const int key = KeyNumber(n);
    return key >= 0 ? key : -1;
}

Trie::Match Trie::LongestPrefix(std::string_view s) const {
    Match m{-1, 0};
    Prefixes(s, [&m](int k, std::size_t length) { m = {k, length}; });
    return m;
}

}  // namespace hdTrans::dict
