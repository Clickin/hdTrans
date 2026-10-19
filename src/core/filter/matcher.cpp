#include "core/filter/matcher.h"

#include <algorithm>

namespace hdTrans::filter {

namespace {

uint64_t EdgeKey(int node, char16_t c) {
    return static_cast<uint64_t>(node) << 32 | static_cast<uint32_t>(c);
}

}  // namespace

int Matcher::Add(std::u16string_view p) {
    const auto [it, added] = ids_.emplace(std::u16string(p), static_cast<int>(count_));
    if (!added) return it->second;
    if (ends_.empty()) ends_.push_back(-1);
    int node = 0;
    for (const char16_t c : p) {
        const auto [e, fresh] = edges_.emplace(EdgeKey(node, c), static_cast<int32_t>(ends_.size()));
        if (fresh) ends_.push_back(-1);
        node = e->second;
    }
    ends_[static_cast<std::size_t>(node)] = static_cast<int32_t>(count_);
    return static_cast<int>(count_++);
}

void Matcher::Build() {
    if (ends_.empty()) ends_.push_back(-1);
    const std::size_t nodes = ends_.size();
    std::vector<std::pair<uint64_t, int32_t>> edges(edges_.begin(), edges_.end());
    std::sort(edges.begin(), edges.end());
    first_.assign(nodes + 1, 0);
    label_.clear();
    child_.clear();
    for (const auto& [key, child] : edges) {
        first_[static_cast<std::size_t>(key >> 32) + 1]++;
        label_.push_back(static_cast<char16_t>(key & 0xFFFFFFFFu));
        child_.push_back(child);
    }
    for (std::size_t n = 0; n < nodes; n++) first_[n + 1] += first_[n];
    std::unordered_map<uint64_t, int32_t>().swap(edges_);
    std::unordered_map<std::u16string, int>().swap(ids_);

    fail_.assign(nodes, 0);
    link_.assign(nodes, -1);
    std::vector<int32_t> queue;
    queue.reserve(nodes);
    for (uint32_t e = first_[0]; e < first_[1]; e++) queue.push_back(child_[e]);
    for (std::size_t q = 0; q < queue.size(); q++) {
        const int32_t node = queue[q];
        for (uint32_t e = first_[static_cast<std::size_t>(node)]; e < first_[static_cast<std::size_t>(node) + 1]; e++) {
            const int32_t next = child_[e];
            int32_t f = fail_[static_cast<std::size_t>(node)];
            int32_t to = Child(f, label_[e]);
            while (to < 0 && f != 0) {
                f = fail_[static_cast<std::size_t>(f)];
                to = Child(f, label_[e]);
            }
            const int32_t target = to < 0 ? 0 : to;
            fail_[static_cast<std::size_t>(next)] = target;
            link_[static_cast<std::size_t>(next)] = ends_[static_cast<std::size_t>(target)] >= 0
                                                        ? target
                                                        : link_[static_cast<std::size_t>(target)];
            queue.push_back(next);
        }
    }
}

int Matcher::Child(int node, char16_t c) const {
    const auto begin = label_.begin() + first_[static_cast<std::size_t>(node)];
    const auto end = label_.begin() + first_[static_cast<std::size_t>(node) + 1];
    const auto it = std::lower_bound(begin, end, c);
    return it != end && *it == c ? child_[static_cast<std::size_t>(it - label_.begin())] : -1;
}

void Matcher::Scan(std::u16string_view text, std::vector<uint32_t>& seen, uint32_t stamp) const {
    if (count_ == 0) return;
    int node = 0;
    for (const char16_t c : text) {
        int next = Child(node, c);
        while (next < 0 && node != 0) {
            node = fail_[static_cast<std::size_t>(node)];
            next = Child(node, c);
        }
        node = next < 0 ? 0 : next;
        for (int at = ends_[static_cast<std::size_t>(node)] >= 0 ? node : link_[static_cast<std::size_t>(node)]; at >= 0;
             at = link_[static_cast<std::size_t>(at)]) {
            uint32_t& s = seen[static_cast<std::size_t>(ends_[static_cast<std::size_t>(at)])];
            if (s == stamp) break;
            s = stamp;
        }
    }
}

}  // namespace hdTrans::filter
