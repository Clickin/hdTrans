#pragma once
// Alternating particles of composed text on the raw path.
#include <string>
#include <string_view>

namespace hdTrans::output {

// Replaces each alternating particle of JosaTable in text by the form that fits the
// text before it. context is the text of the preceding item (hasContext false when
// there is none); it decides the particle at the start of text and, when it contains
// '%', text is cut after its first ')' instead. Texts shorter than 6 width units are
// returned unchanged.
//
// The ASCII letters an ending is read from are taken at width offsets computed in one
// of the two texts and read in the other; positions outside a text read 0.
std::string ApplyJosaAlternation(std::string_view text, std::string_view context, bool hasContext);

}  // namespace hdTrans::output
