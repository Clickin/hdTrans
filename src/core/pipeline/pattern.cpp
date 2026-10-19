#include "core/pipeline/pattern.h"

namespace hdTrans::pipeline::internal {

bool Find(std::string_view s, size_t from, Pattern p, Match& m) {
    for (size_t pos = from; pos <= s.size();) {
        m = Match{};
        if (p(s, pos, m)) {
            m.begin = pos;
            m.group[0] = Span(s, pos, m.end);
            return true;
        }
        if (pos == s.size()) break;
        pos += At(s, pos).size;
    }
    return false;
}

Match Rematch(const Match& m, Pattern p) {
    Match g;
    if (!Find(m.group[0], 0, p, g)) g = Match{};
    return g;
}

std::string Cat(std::initializer_list<std::string_view> parts) {
    std::string out;
    for (std::string_view p : parts) out.append(p);
    return out;
}

}  // namespace hdTrans::pipeline::internal
