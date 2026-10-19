#pragma once
// String matching of the rule operators, used by the Check methods of an Item.
// Surfaces and patterns are UTF-8 text; codes and attributes are ASCII and compared
// byte by byte.
#include <span>
#include <string_view>

namespace hdTrans::rules {

// A surface (S, D, U, Y) against a rule pattern. The pattern's leading marker selects
// the test:
//   "L" v or "11" v   the surface starts with v
//   "R" v or "22" v   the surface ends with v
//   "00" v            the surface occurs inside v
//   "99" v            the surface is v
//   anything else     the surface is the pattern
bool MatchRuleSurface(std::string_view surface, std::string_view pattern);

// A pumsa code (P, M) against a pattern of 3-byte alternatives ("58*5:0" is "58*" or
// "5:0"; the last may be shorter). An alternative matches when the code is at least
// as long and each of its bytes is '*' or the code's byte.
bool MatchRuleCode(std::string_view code, std::string_view pattern);

// Whether a whole 3-byte code of values matches name, '*' in name matching any byte.
bool HasCode(std::string_view name, std::string_view values);

// Whether values (3-byte semantic codes) holds a code of the family: its name with
// the '*' positions of pattern's first three bytes wildcarded, a code of a parent
// family (with the same pattern), or the name of an expansion family. A family with
// five parents has its expansions ignored.
bool MatchAttributeFamily(int family, std::string_view values, std::string_view pattern);

// A == for one candidate: a 3-byte chunk of pattern occurs anywhere in values.
bool AttrChunksIn(std::string_view values, std::string_view pattern);

// T == for one candidate: a family selected by a 3-byte chunk of pattern matches
// values, with pattern's wildcards.
bool GobiAttrValues(std::string_view values, std::string_view pattern);

// A -= for one candidate: for a 3-byte chunk c of pattern other than "XXX" and a
// family c selects ('*' and 'X' wildcards), a 3-byte code of values other than "XXX"
// belongs to the family with c as the pattern.
bool MinAttrValues(std::string_view values, std::string_view pattern);

// T -= for one candidate: MinAttrValues where 'X' is no wildcard and an "XXX" chunk
// is not skipped.
bool MinGobiAttrValues(std::string_view values, std::string_view pattern);

// G -= for one candidate: a 2-byte chunk of values occurs in pattern.
bool MinGAttrValues(std::string_view values, std::string_view pattern);

namespace internal {

// A semantic code family. parent and expansion list family indexes, -1 terminated
// (a full parent list has no terminator).
struct AttributeFamily {
    const char* name;
    int parent[5];
    int expansion[8];
};

std::span<const AttributeFamily> AttributeFamilies();

}  // namespace internal

}  // namespace hdTrans::rules
