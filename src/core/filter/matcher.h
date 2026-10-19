#pragma once
// Finds which of many fixed strings occur in a text, in one pass (Aho-Corasick).
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hdTrans::filter {

class Matcher {
public:
    // The number of pattern p, the same for equal patterns. p is not empty. Patterns are
    // added before Build.
    int Add(std::wstring_view p);
    void Build();
    std::size_t Size() const { return count_; }

    // Sets seen[i] = stamp for every pattern i that occurs in text; seen has Size()
    // elements.
    void Scan(std::wstring_view text, std::vector<uint32_t>& seen, uint32_t stamp) const;

private:
    int Child(int node, wchar_t c) const;

    std::unordered_map<std::wstring, int> ids_;
    std::size_t count_ = 0;
    // The trie while patterns are added: (node << 32 | char) -> child.
    std::unordered_map<uint64_t, int32_t> edges_;
    std::vector<int32_t> ends_;  // per node: the pattern ending there, -1 for none
    // After Build, the edges of node n are first_[n]..first_[n + 1] of label_/child_,
    // sorted by label.
    std::vector<uint32_t> first_;
    std::vector<wchar_t> label_;
    std::vector<int32_t> child_;
    std::vector<int32_t> fail_;
    std::vector<int32_t> link_;  // the nearest node on the fail chain with a pattern end, -1 for none
};

}  // namespace hdTrans::filter
