#pragma once
// The regular expressions of filter rows: ECMAScript as std::wregex reads it.
//
// The library is built without exceptions, and std::wregex reports a malformed
// pattern only by throwing, so every pattern is checked here first and only a pattern
// that passes is compiled. The check accepts a subset of ECMAScript that the standard
// libraries of every supported compiler compile alike; it rejects among others
// lookbehind "(?<=" "(?<!", "(?" with anything but ":", "=" or "!", possessive and
// stacked quantifiers, a quantifier after an assertion, a "]" "{" "}" outside a
// character class that is not part of a quantifier, a reversed or class-bounded range,
// "[:" "[." "[=" inside a class, an escape of a letter, digit or "_" that ECMAScript
// does not define, an escape of a non-ASCII character, a back-reference to a group not
// yet opened (inside a lookahead, a group of that lookahead), and an unbounded
// repetition of a group that holds an unbounded repetition.
#include <string>
#include <string_view>

namespace hdTrans::filter {

// Whether p passes. On success literal is a substring of every match (empty when the
// pattern has none, or several alternatives at the top level); on failure error says
// why.
bool CheckPattern(std::wstring_view p, std::wstring& literal, std::string& error);

}  // namespace hdTrans::filter
