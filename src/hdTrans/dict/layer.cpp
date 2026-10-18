#include "hdTrans/dict/layer.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "hdTrans/dict/runes.h"

namespace hdTrans::dict {

// Writes the arrays of a Dict.
struct DictBuilder {
    static Dict Build(std::vector<internal::Row>& rows, const std::vector<int32_t>& first, const CodeTable& codes);
};

namespace {

std::size_t UvarintSize(uint64_t x) {
    std::size_t n = 1;
    for (; x >= 0x80; x >>= 7) n++;
    return n;
}

void AppendUvarint(std::string& b, uint64_t x) {
    for (; x >= 0x80; x >>= 7) b.push_back(static_cast<char>((x & 0x7F) | 0x80));
    b.push_back(static_cast<char>(x));
}

struct KeyClass {
    std::string_view key;
    char cls;
    bool operator==(const KeyClass&) const = default;
};

struct KeyClassHash {
    std::size_t operator()(const KeyClass& k) const {
        return std::hash<std::string_view>()(k.key) * 31 + static_cast<unsigned char>(k.cls);
    }
};

}  // namespace

namespace internal {

void Layer::Add(std::string_view key, std::string_view target, int32_t code, int32_t prio, int32_t rule, int32_t id) {
    if (!internal::ValidUtf8(key)) {
        std::string fixed;
        for (std::size_t i = 0; i < key.size();) {
            const auto r = text::DecodeRune(key.substr(i));
            text::AppendRune(fixed, r.cp);
            i += r.size;
        }
        key = *repaired_.emplace_back(std::make_unique<std::string>(std::move(fixed)));
    }
    rows_.push_back({key, target, code, prio, rule, id});
}

void Layer::Append(Layer&& other) {
    if (rows_.empty()) {
        rows_.swap(other.rows_);
    } else {
        rows_.insert(rows_.end(), other.rows_.begin(), other.rows_.end());
    }
    for (auto& r : other.repaired_) repaired_.push_back(std::move(r));
    other.rows_.clear();
    other.repaired_.clear();
}

void Layer::ByPrio(std::size_t start) {
    const auto begin = rows_.begin() + static_cast<std::ptrdiff_t>(start);
    if (std::none_of(begin, rows_.end(), [](const Row& r) { return r.prio != 0; })) return;
    std::stable_sort(begin, rows_.end(), [](const Row& a, const Row& b) { return a.prio > b.prio; });
}

void Layer::Shadow(std::size_t end, const std::vector<uint32_t>* later, const CodeTable& codes) {
    const std::size_t laterCount = later ? later->size() : rows_.size() - end;
    if (end == 0 || laterCount == 0) return;
    auto keyClass = [&](const Row& r) {
        const PartCode p = codes.At(r.code)->part;
        return KeyClass{r.key, p.Size() > 1 ? p.ClassDigit() : '\0'};
    };
    auto laterRow = [&](std::size_t j) -> const Row& { return rows_[later ? (*later)[j] : end + j]; };
    if (end < laterCount) {
        std::unordered_map<KeyClass, std::vector<uint32_t>, KeyClassHash> earlier;
        earlier.reserve(end);
        for (std::size_t i = 0; i < end; i++) {
            if (rows_[i].code != kDeleted) earlier[keyClass(rows_[i])].push_back(static_cast<uint32_t>(i));
        }
        for (std::size_t j = 0; j < laterCount; j++) {
            const Row& r = laterRow(j);
            if (r.code == kDeleted) continue;
            const auto it = earlier.find(keyClass(r));
            if (it == earlier.end()) continue;
            for (const uint32_t i : it->second) rows_[i].code = kDeleted;
        }
        return;
    }
    std::unordered_set<KeyClass, KeyClassHash> shadowed;
    shadowed.reserve(laterCount);
    for (std::size_t j = 0; j < laterCount; j++) {
        const Row& r = laterRow(j);
        if (r.code != kDeleted) shadowed.insert(keyClass(r));
    }
    for (std::size_t i = 0; i < end; i++) {
        if (rows_[i].code != kDeleted && shadowed.count(keyClass(rows_[i]))) rows_[i].code = kDeleted;
    }
}

void Layer::Compact() {
    std::erase_if(rows_, [](const Row& r) { return r.code == kDeleted; });
}

// Orders the rows by key, then, when prio_, by prio, higher first, keeping the order
// of equal ones (a stable sort). The rows are usually a few long runs in key order,
// one per file: runs are merged as they are, short runs are sorted together first.
void Layer::Sort() {
    auto less = [this](const Row& a, const Row& b) {
        if (const int c = a.key.compare(b.key); c != 0) return c < 0;
        return prio_ && a.prio > b.prio;
    };
    constexpr std::size_t kShortRun = 256;
    std::vector<std::size_t> segments{0};  // segment i is rows_[segments[i], segments[i+1])
    const std::size_t n = rows_.size();
    for (std::size_t start = 0; start < n;) {
        std::size_t end = start + 1;
        while (end < n && !less(rows_[end], rows_[end - 1])) end++;
        if (end - start < kShortRun) {
            // Join the following short runs.
            std::size_t stop = end;
            while (stop < n) {
                std::size_t e = stop + 1;
                while (e < n && !less(rows_[e], rows_[e - 1])) e++;
                if (e - stop >= kShortRun) break;
                stop = e;
            }
            end = stop;
            std::stable_sort(rows_.begin() + static_cast<std::ptrdiff_t>(start),
                             rows_.begin() + static_cast<std::ptrdiff_t>(end), less);
        }
        segments.push_back(end);
        start = end;
    }
    std::vector<Row> buf;
    while (segments.size() > 2) {
        std::vector<std::size_t> merged{0};
        buf.resize(n);
        std::size_t i = 0;
        for (; i + 2 < segments.size(); i += 2) {
            const auto lo = rows_.begin() + static_cast<std::ptrdiff_t>(segments[i]);
            const auto mid = rows_.begin() + static_cast<std::ptrdiff_t>(segments[i + 1]);
            const auto hi = rows_.begin() + static_cast<std::ptrdiff_t>(segments[i + 2]);
            std::merge(lo, mid, mid, hi, buf.begin() + static_cast<std::ptrdiff_t>(segments[i]), less);
            merged.push_back(segments[i + 2]);
        }
        if (i + 1 < segments.size()) {
            std::copy(rows_.begin() + static_cast<std::ptrdiff_t>(segments[i]), rows_.end(),
                      buf.begin() + static_cast<std::ptrdiff_t>(segments[i]));
            merged.push_back(n);
        }
        rows_.swap(buf);
        segments = std::move(merged);
    }
}

Dict Layer::Build(const CodeTable& codes) {
    Sort();
    std::vector<int32_t> first;
    first.reserve(rows_.size() / 2 + 1);
    for (std::size_t i = 0; i < rows_.size(); i++) {
        if (i == 0 || rows_[i].key != rows_[i - 1].key) first.push_back(static_cast<int32_t>(i));
    }
    first.push_back(static_cast<int32_t>(rows_.size()));
    return DictBuilder::Build(rows_, first, codes);
}

}  // namespace internal

Dict DictBuilder::Build(std::vector<internal::Row>& rows, const std::vector<int32_t>& first, const CodeTable& codes) {
    struct Context {
        const std::vector<internal::Row>* rows;
        const std::vector<int32_t>* first;
    } ctx{&rows, &first};
    const std::size_t count = first.size() - 1;
    Dict d;
    std::vector<int32_t> order;
    d.keys_ = Trie::Build(
        count,
        [](const void* c, std::size_t i) {
            const auto* x = static_cast<const Context*>(c);
            return (*x->rows)[static_cast<std::size_t>((*x->first)[i])].key;
        },
        &ctx, &order);

    // index[code] is the index in d.codes_ of the codes numbered code.
    std::vector<int32_t> uses(codes.Size());
    for (const auto& r : rows) uses[static_cast<std::size_t>(r.code)]++;
    std::vector<int32_t> used;
    for (std::size_t id = 0; id < uses.size(); id++) {
        if (uses[id] > 0) used.push_back(static_cast<int32_t>(id));
    }
    std::stable_sort(used.begin(), used.end(), [&](int32_t a, int32_t b) {
        return uses[static_cast<std::size_t>(a)] > uses[static_cast<std::size_t>(b)];
    });
    std::vector<int32_t> index(codes.Size());
    d.codes_.resize(used.size());
    for (std::size_t i = 0; i < used.size(); i++) {
        index[static_cast<std::size_t>(used[i])] = static_cast<int32_t>(i);
        d.codes_[i] = codes.At(used[i]);
    }

    auto recordSize = [&](const internal::Row& r) {
        const uint64_t h = uint64_t{r.target.size()} << 2;
        std::size_t n = UvarintSize(h | 3) + r.target.size() +
                        UvarintSize(static_cast<uint64_t>(index[static_cast<std::size_t>(r.code)])) +
                        UvarintSize(static_cast<uint32_t>(r.id));
        if (r.rule != 0) n += UvarintSize(static_cast<uint32_t>(r.rule));
        return n;
    };
    std::size_t size = 0;
    for (const auto& r : rows) size += recordSize(r);
    d.blob_.reserve(size);
    d.starts_.reserve((order.size() + Dict::kKeysPerStart - 1) / Dict::kKeysPerStart);
    for (std::size_t k = 0; k < order.size(); k++) {
        if (k % Dict::kKeysPerStart == 0) d.starts_.push_back(static_cast<uint32_t>(d.blob_.size()));
        const auto key = static_cast<std::size_t>(order[k]);
        const auto lo = static_cast<std::size_t>(first[key]);
        const auto hi = static_cast<std::size_t>(first[key + 1]);
        for (std::size_t i = lo; i < hi; i++) {
            const internal::Row& r = rows[i];
            uint64_t h = uint64_t{r.target.size()} << 2;
            if (r.rule != 0) h |= 2;
            if (i + 1 < hi) h |= 1;
            AppendUvarint(d.blob_, h);
            d.blob_.append(r.target);
            AppendUvarint(d.blob_, static_cast<uint64_t>(index[static_cast<std::size_t>(r.code)]));
            if (r.rule != 0) AppendUvarint(d.blob_, static_cast<uint32_t>(r.rule));
            AppendUvarint(d.blob_, static_cast<uint32_t>(r.id));
        }
    }
    return d;
}

}  // namespace hdTrans::dict
