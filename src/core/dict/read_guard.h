#pragma once

#include <cstddef>

namespace hdTrans::dict {

struct ReadGuard {
    const void* context = nullptr;
    bool (*check)(const void*, const void*, std::size_t) = nullptr;
    void (*fail)(const void*) = nullptr;

    bool Check(const void* data = nullptr, std::size_t size = 0) const {
        return !check || check(context, data, size);
    }
    bool Fail() const {
        if (fail) fail(context);
        return false;
    }
};

}  // namespace hdTrans::dict
