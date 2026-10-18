#pragma once
// Ruby markup and kana glosses of a line, as the name evidence reads them.
#include <cstddef>
#include <string>
#include <string_view>

namespace hdTrans::names::internal {

// A match of ruby markup or of a kana gloss: the base text and the reading, as byte ranges
// of the line.
struct RubyMatch {
    std::size_t begin = 0, end = 0;
    std::string_view base, reading;
};

// The leftmost ruby markup in s at or after from: |漢字《かな》, 漢字《かな》 or HTML
// <ruby>. base and reading are the parts of the alternative that matched.
bool FindRuby(std::string_view s, std::size_t from, RubyMatch& m);

// The leftmost kana gloss in s at or after from: 漢字（かな） and カタカナ(かな).
bool FindGloss(std::string_view s, std::size_t from, RubyMatch& m);

}  // namespace hdTrans::names::internal
