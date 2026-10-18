#include "hdTrans/word/document_senses.h"

#include "hdTrans/text/utf8.h"
#include "hdTrans/word/worker.h"

namespace hdTrans::word {
namespace {

constexpr int kVariant = 1;

bool EligibleKey(const Item& it, int candidate) {
    const auto code = it.Code(candidate);
    if (code.empty() || (code[0] != '1' && code[0] != '4' && code[0] != '7' && code[0] != '8')) return false;
    if (kVariant < 2) return true;
    std::string_view key = it.group->surface;
    if (text::RuneCount(key) < 2) return false;
    while (!key.empty()) {
        const auto rune = text::DecodeRune(key);
        if ((rune.cp < 0x3040 || rune.cp > 0x309f) && rune.cp != U'ー') return true;
        key.remove_prefix(rune.size);
    }
    return false;
}

std::string PartCode(const Item& it, int candidate) {
    return std::string(1, it.group->At(candidate)->cls) + std::string(it.Code(candidate));
}

}  // namespace

void SessionSenses::FinishLine() {
    choices.merge(pending);
    pending.clear();
}

void SessionSenses::Remember(const Item& it) {
    if (!EligibleKey(it, it.selected)) return;
    switch (it.choice.layer) {
        case Provenance::GlobalRule:
        case Provenance::SenseDefault:
            if (kVariant >= 3) return;
            [[fallthrough]];
        case Provenance::LocalRule:
        case Provenance::Sense:
            if (!choices.contains(it.group->surface)) {
                pending.try_emplace(it.group->surface,
                                    SessionSense{std::string(it.EntryText(it.selected)), PartCode(it, it.selected)});
            }
            break;
        default:
            break;
    }
}

int DocumentSenseScore(const Item& it, int candidate, const SelectContext&) {
    const SessionSenses* senses = it.w->settings.sessionSenses;
    if (senses == nullptr || !EligibleKey(it, candidate)) return 0;
    const auto found = senses->choices.find(it.group->surface);
    if (found == senses->choices.end() || found->second.target != it.EntryText(candidate)) return 0;
    if (kVariant >= 4 && (found->second.partCode != PartCode(it, it.selected) ||
                          found->second.partCode != PartCode(it, candidate))) return 0;
    return 1;
}

}  // namespace hdTrans::word
