#include "hdTrans/engine/rules.h"

#include <string>

namespace hdTrans::internal {

namespace {

struct Loaded {
    rules::Program program;
    bool ok = false;
};

const Loaded& Load() {
    static const Loaded loaded = [] {
        std::string text;
        for (std::string_view line : RuleLines()) text += line;
        Loaded l;
        rules::LoadError error{};
        l.ok = rules::Load(text, l.program, error);
        return l;
    }();
    return loaded;
}

}  // namespace

const rules::Program& RulesProgram() { return Load().program; }

bool RulesLoaded() { return Load().ok; }

}  // namespace hdTrans::internal
