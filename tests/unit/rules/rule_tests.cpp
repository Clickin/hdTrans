#include "rules/rule_tests.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "core/dict/snapshot.h"
#include "core/engine/driver.h"
#include "core/engine/rules.h"
#include "core/word/item.h"
#include "hdtest.h"

using namespace hdTrans;

namespace ruletests {

namespace {

struct Dictionaries {
    std::shared_ptr<const dict::Snapshot> snapshot;
    std::shared_ptr<internal::Generated> generated;
};

const Dictionaries* Loaded() {
    static const Dictionaries d = [] {
        Dictionaries loaded;
        const char* dir = std::getenv("HDTRANS_DICTS");
        if (dir == nullptr || *dir == 0) return loaded;
        loaded.snapshot = dict::LoadFolders({dir}).snapshot;
        if (loaded.snapshot) loaded.generated = internal::NewGenerated(*loaded.snapshot);
        return loaded;
    }();
    return d.snapshot ? &d : nullptr;
}

// The targets of the words whose candidate or ending the rule chose.
struct Picks {
    word::Selection rule;
    std::vector<std::string> targets;
};

void Resolved(void* context, int, int, const word::Chain& chain) {
    Picks& p = *static_cast<Picks*>(context);
    for (const word::Item* it = chain.head; it != nullptr; it = it->next) {
        if (it->choice.layer == p.rule.layer && it->choice.id == p.rule.id)
            p.targets.emplace_back(it->EntryText(it->selected));
        if (it->directChoice.layer == p.rule.layer && it->directChoice.id == p.rule.id)
            p.targets.emplace_back(it->DirectText(it->DirectIndex()));
    }
}

std::string Translate(const Dictionaries& d, const std::string& input, const rules::Program* program, Picks* picks) {
    internal::Memory memory;
    internal::StageObserver observer;
    if (picks) {
        observer.context = picks;
        observer.resolved = Resolved;
    }
    internal::TranslationOptions options;
    options.rules = program;
    const std::vector<std::string> lines{input};
    const std::vector<std::string> out = internal::TranslateDocument(*d.snapshot, *d.generated, lines, memory, observer, options);
    return out.empty() ? std::string() : out[0];
}

// The built-in program with one rule emptied (||0).
rules::Program Without(const Case& c) {
    rules::Program p = internal::RulesProgram();
    const uint32_t root = static_cast<uint32_t>(p.nodes.size());
    p.nodes.push_back({rules::NodeKind::Or, rules::Op::SrcEq, 0, 0, 0, root + 1});
    const std::size_t i = static_cast<std::size_t>(c.index);
    if (c.kind == 'L') {
        p.localRules[i] = root;
        if (i < p.localCode.size()) p.localCode[i] = nullptr;
    } else {
        p.globalRules[i].root = root;
        if (i < p.globalCode.size()) p.globalCode[i] = nullptr;
    }
    return p;
}

std::string Quote(const std::string& s) { return "\"" + s + "\""; }

std::string List(const std::vector<std::string>& v) {
    std::string out;
    for (const std::string& s : v) out += (out.empty() ? "" : ", ") + Quote(s);
    return out;
}

}  // namespace

void Run(const Case& c) {
    const Dictionaries* d = Loaded();
    if (d == nullptr) return hdtest::Skip("HDTRANS_DICTS is not set");
    const std::size_t count = c.kind == 'L' ? internal::RulesProgram().localRules.size() : internal::RulesProgram().globalRules.size();
    if (c.index < 0 || static_cast<std::size_t>(c.index) >= count) return hdtest::Fail(c.file, c.line, "no such rule");
    const std::string what = std::string("rule ") + c.kind + " " + std::to_string(c.index) + ": " + Quote(c.input);
    Picks picks{{c.kind == 'L' ? word::Provenance::LocalRule : word::Provenance::GlobalRule, c.kind == 'L' ? c.index : c.number}, {}};
    const std::string got = Translate(*d, c.input, nullptr, &picks);
    switch (c.test) {
    case Kind::Text:
        if (got != c.expected) hdtest::Fail(c.file, c.line, what + " -> expected " + Quote(c.expected) + ", got " + Quote(got));
        break;
    case Kind::Unchanged: {
        const rules::Program without = Without(c);
        const std::string other = Translate(*d, c.input, &without, nullptr);
        if (got != other)
            hdtest::Fail(c.file, c.line, what + " -> expected the same translation without the rule; with it " + Quote(got) +
                                             ", without it " + Quote(other));
        break;
    }
    case Kind::Picks: {
        bool found = false;
        for (const std::string& t : picks.targets) found = found || t == c.expected;
        if (!found)
            hdtest::Fail(c.file, c.line, what + " -> expected the rule to choose " + Quote(c.expected) + ", it chose " +
                                             (picks.targets.empty() ? std::string("nothing") : List(picks.targets)) +
                                             " (translation " + Quote(got) + ")");
        break;
    }
    case Kind::Silent:
        if (!picks.targets.empty())
            hdtest::Fail(c.file, c.line, what + " -> expected the rule to choose nothing, it chose " + List(picks.targets) +
                                             " (translation " + Quote(got) + ")");
        break;
    }
}

}  // namespace ruletests
