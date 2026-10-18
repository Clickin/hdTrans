#pragma once
// Conjugating Korean targets: a Korean lemma written as its stem followed by a marker
// family of the marker tables ("공부&AV@" for 공부하다), as the plain dictionaries,
// the sense table and the negatives of conjugating entries compile them.
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace hdTrans::userdict {

// The conjugation classes a plain-dictionary line may name after its POS.
inline constexpr std::string_view kClasses[] = {"규칙", "하다", "되다", "이다", "ㄹ", "ㅂ", "ㄷ", "ㅅ", "르", "러", "ㅎ", "우", "으"};

// The form codes of a family, '0'..'9', 'A', 'B', by index.
inline constexpr std::string_view kFormCodes = "0123456789AB";

// The rows of a family by form code index; "" for a form the family lacks.
using FamilyRows = std::array<std::string, 12>;

// The marker families made for stems that no static family conjugates. A family is
// identified by its marker and rows, so the same rows always get the same id; ids
// ("&Ba@") use first letters that no static family of the marker uses, and are
// numbered in the order of their first Generate.
class Families {
public:
    // The row of key ("&A1@0", a family id and a form code) in the &, $ or % table,
    // else among the generated families (of any marker); "" when there is none.
    std::string_view SourceRow(std::string_view key) const;
    std::string_view DollarRow(std::string_view key) const;
    std::string_view PercentRow(std::string_view key) const;

    // The id of the family of marker ('&', '$' or '%') with rows, made when new.
    // Returns an error message, empty on success.
    std::string Generate(char marker, const FamilyRows& rows, std::string& id);

    // The number of generated families.
    std::size_t Size() const { return ids_.size(); }
    std::size_t MemoryBytes() const;

private:
    std::string_view Generated(std::string_view key) const;

    std::unordered_map<std::string, std::string> rows_;  // "&Ba@5" -> row
    std::unordered_map<std::string, std::string> ids_;   // marker and rows -> "&Ba@"
    std::array<int, 3> count_{};                         // families made per marker & $ %
};

// The target of lemma (a Korean word ending in 다) for marker '&' (verbs), '$'
// (i-adjectives) or '%' (adjectival nouns): its final syllables replaced by a static
// family that conjugates the stem in class, else by a generated one. class is one of
// kClasses, or empty to take the class the spelling implies (듣다 ㄷ, 돕다 ㅂ, 짓다 ㅅ,
// 푸다 우, a final ㄹ, 르 and 으 stems; any other stem is regular). Returns an error
// message, empty on success.
std::string Conjugating(std::string_view lemma, char marker, std::string_view cls, Families& families,
                        std::string& target);

}  // namespace hdTrans::userdict
