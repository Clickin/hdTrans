#include "core/engine/rules.h"

#include <string>

#include "core/rules/compiled.h"

namespace hdTrans::internal {

namespace {

struct Loaded {
    rules::Program program;
    bool ok = false;
};

#ifndef HDTRANS_INTERPRET_RULES
// Gives each rule the compiled code made from its record line, when that line is unchanged.
void AttachCompiled(rules::Program& program) {
    const auto local = rules::compiled::LocalRules();
    const auto global = rules::compiled::GlobalRules();
    std::size_t l = 0, g = 0;
    for (std::string_view line : RuleLines()) {
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);
        const bool isLocal = line.starts_with("L "), isGlobal = line.starts_with("G ");
        if (!isLocal && !isGlobal) continue;
        const std::size_t index = isLocal ? l++ : g++;
        const auto table = isLocal ? local : global;
        if (index >= table.size() || table[index].code == nullptr) continue;
        if (table[index].hash != rules::compiled::RecordHash(line)) continue;
        auto& code = isLocal ? program.localCode : program.globalCode;
        if (code.size() <= index) code.resize(index + 1, nullptr);
        code[index] = table[index].code;
    }
}
#endif

const Loaded& Load() {
    static const Loaded loaded = [] {
        std::string text;
        for (std::string_view line : RuleLines()) text += line;
        Loaded l;
        rules::LoadError error{};
        l.ok = rules::Load(text, l.program, error);
#ifndef HDTRANS_INTERPRET_RULES
        if (l.ok) AttachCompiled(l.program);
#endif
        return l;
    }();
    return loaded;
}

}  // namespace

const rules::Program& RulesProgram() { return Load().program; }

bool RulesLoaded() { return Load().ok; }

}  // namespace hdTrans::internal
