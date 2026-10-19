#pragma once
// The senses of one dictionary while it is loaded, until Build makes the Dict.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/dict.h"

namespace hdTrans::dict::internal {

// A sense in load order. key and target point into the loaded files (or into the
// layer for repaired keys); the files must outlive the layer.
struct Row {
    std::string_view key;
    std::string_view target;
    int32_t code;  // index in the CodeTable; kDeleted for a row Compact drops
    int32_t prio;
    int32_t rule;
    int32_t id;
};

class Layer {
public:
    static constexpr int32_t kDeleted = -1;

    // prio: Build orders the records of a key by prio, higher first.
    explicit Layer(bool prio) : prio_(prio) {}

    // Appends a row. A byte of key that is not valid UTF-8 is stored as U+FFFD, as
    // lookups decode it.
    void Add(std::string_view key, std::string_view target, int32_t code, int32_t prio, int32_t rule, int32_t id);

    std::vector<Row>& Rows() { return rows_; }
    const std::vector<Row>& Rows() const { return rows_; }

    // Orders the rows from start by prio, higher first, keeping the order of equal
    // ones.
    void ByPrio(std::size_t start);
    // Marks deleted the rows of [0, end) whose key and class digit a row of later
    // (indexes in Rows; all rows from end when null) has. Compact drops them.
    void Shadow(std::size_t end, const std::vector<uint32_t>* later, const CodeTable& codes);
    void Compact();
    // Appends the rows of other.
    void Append(Layer&& other);

    // Builds the dictionary of the rows. The records of a key are ordered by prio
    // when the layer orders by prio, then by row order.
    Dict Build(const CodeTable& codes);

private:
    void Sort();

    bool prio_;
    std::vector<Row> rows_;
    std::vector<std::unique_ptr<std::string>> repaired_;  // keys rows point to
};

}  // namespace hdTrans::dict::internal
