#pragma once
// The pre-filter and the post-filter: ordered text replacements over the Japanese input
// of a line before it is translated and over the Korean output after it, read from
// filter files in the row format of Ehnd (docs/ehnd-compat.md).
//
// A stage runs its rows in order of layer, then of load order; each row sees the text
// as the rows before it left it. Literal rows are found with one matcher over all their
// sources; a regex row runs std::wregex only when the text holds the row's _AUX text
// and the literal its pattern requires. Toggles live per line: Pre may set them for
// Post, and the next line starts again from the declared states.
#include <cstdint>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "core/dict/dict.h"
#include "core/filter/matcher.h"

namespace hdTrans::filter {

enum class Stage : uint8_t { Pre, Post };

// A file of a filter set; texts are UTF-8.
struct Source {
    enum class Kind : uint8_t { PreFilter, PostFilter, SkipLayer, UserDict };
    Kind kind = Kind::PreFilter;
    std::string name;
    std::string text;
    // A file of a dictionary folder, where a line starting with "#" is a comment too.
    bool native = false;
};

// A line or a file that does not load.
struct Error {
    std::string file;
    int line = 0;  // 1-based; 0 for the whole file
    std::string message;
};

class Filters {
public:
    // The toggle states of one line.
    using Toggles = std::vector<char>;
    Toggles Start() const { return defaults_; }

    // Rewrites line (without "\n") with the rows of stage; an empty line stays empty.
    void Apply(Stage stage, std::string& line, Toggles& toggles) const;

    // The user dictionary records of the UserDict files; null for none.
    const dict::Dict* User() const { return user_.Len() > 0 ? &user_ : nullptr; }

    // Rows per stage, for reports.
    std::size_t Rows(Stage stage) const { return (stage == Stage::Pre ? pre_ : post_).rules.size(); }

private:
    friend std::shared_ptr<const Filters> Build(const std::vector<Source>& sources, std::vector<Error>& errors);

    enum class Kind : uint8_t { Plain, Guard, Guarded };

    struct Rule {
        std::wstring src, dest;
        int layer = 0;
        int32_t regex = -1;    // the index in regexes; -1 for a literal row
        int32_t pattern = -1;  // the matcher pattern of src, or of the literal a regex requires; -1 for none
        int32_t aux = -1;      // the matcher pattern of the _AUX text; -1 for none
        int32_t gate = -1;     // the toggle that must be on (_CONN); -1 for none
        int32_t sets = -1;     // the toggle a firing row sets (_ON) or clears (_OFF); -1 for none
        bool on = false;
        bool check = false;    // DEST _MATCH_CHECK: the row only tests
        bool blockEnd = false; // a _skip_sub row whose next row is not one
        Kind kind = Kind::Plain;
    };

    // A SkipLayer condition: the rows of layer run only when cond matches.
    struct Condition {
        int layer = 0;
        int32_t regex = -1;
        int32_t pattern = -1;  // the literal cond requires; -1 for none
    };

    struct Program {
        std::vector<Rule> rules;
        std::vector<Condition> conditions;  // ordered by layer
        Matcher matcher;
    };

    void Run(const Program& p, std::wstring& text, Toggles& toggles) const;

    Program pre_, post_;
    std::vector<std::wregex> regexes_;
    Toggles defaults_;
    dict::CodeTable codes_;
    dict::Dict user_;
};

// The filters of sources, which are in load order; null for no sources. Lines that do
// not load are skipped and listed in errors.
std::shared_ptr<const Filters> Build(const std::vector<Source>& sources, std::vector<Error>& errors);

// UTF-8 to and from wide strings (UTF-16 or UTF-32, as wchar_t is); invalid bytes read
// as U+FFFD.
std::wstring Wide(std::string_view s);
std::string Narrow(std::wstring_view s);

}  // namespace hdTrans::filter
