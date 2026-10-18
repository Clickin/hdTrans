// The terminal pass: a bare attributive verb or adjective becomes the terminal form of
// the same tense when what follows it ends the sentence or a clause.
#include <string>
#include <string_view>

#include "hdTrans/word/flush.h"
#include "hdTrans/word/terminal.h"
#include "hdTrans/word/strings.h"
#include "hdTrans/word/util.h"

namespace hdTrans::word {
namespace {

constexpr std::string_view kCopulaStarts[] = {"で", "じゃ", "に", "を", "が", "へ", "っ"};
constexpr std::string_view kPhraseParticles[] = {"が", "を", "は", "に", "で", "と", "も", "へ", "の"};

bool IsOneOfClosing(const Item* it) { return it != nullptr && InSet(kClosingQuoteItems, it->group->surface); }

bool IsOneOfCommas(const Item* it) { return it != nullptr && InSet(kCommaItems, it->group->surface); }

bool IsOneOfSentenceEnds(const Item* it) { return it != nullptr && InSet(kSentenceEndItems, it->group->surface); }

// A verb or adjective item that does not start with a noun.
bool IsPredicate(const Item* it) {
    const std::string_view code = it->Code(it->selected);
    const char cls = At(code, 0);
    if ((cls != '4' && cls != '7') || (cls == '4' && At(code, 2) == '9')) return false;
    std::string_view entry = it->EntryText(it->selected);
    while (!entry.empty() && entry.front() == ' ') entry.remove_prefix(1);
    if (entry.starts_with(kKoGeot)) return false;
    const std::string_view s = it->group->surface;
    for (const std::string_view p : kCopulaStarts) {
        if (s.starts_with(p)) return false;
    }
    // A particle after the first character makes a phrase that starts with a noun.
    for (std::size_t i = FirstChar(s).size(); i < s.size();) {
        const std::string_view r = FirstChar(s.substr(i));
        if (InSet(kPhraseParticles, r)) return false;
        i += r.size();
    }
    return true;
}

// The form code of the nominal ending of a verb or an adjective; 0 for others.
char NominalForm(char cls) {
    if (cls == '4') return 'B';
    if (cls == '7') return '8';
    return 0;
}

// A run of predicate items starting at it whose last item ends the sentence (past
// closing quotes), or ends in 다 before a comma.
bool EndsClause(const Item* it) {
    if (!IsPredicate(it)) return false;
    while (it->next != nullptr && IsPredicate(it->next)) it = it->next;
    const std::string_view ending = it->DirectText(it->DirectIndex());
    if (At(ending, 0) == NominalForm(At(it->Code(it->selected), 0))) return false;
    const Item* next = it->next;
    while (IsOneOfClosing(next)) next = next->next;
    if (next == nullptr || IsOneOfSentenceEnds(next)) return true;
    return IsOneOfCommas(next) && it->DirectIndex() >= 0 && it->DirectText(it->DirectIndex()).ends_with(kKoDa);
}

}  // namespace

void PreferTerminal(Flusher& f) {
    for (Item* it = f.w.chain.tail; it != nullptr; it = it->prev) {
        if (it->DirectIndex() < 0 || OwnedGroup(it->DirectGroup()) || !Has(it->EntryText(it->selected), "@")) continue;
        const char cls = At(it->Code(it->selected), 0);
        const std::string_view text = it->DirectText(it->DirectIndex());
        const std::string ja = it->group->surface + std::string(it->DirectSurface());
        for (const TerminalForm& form : kTerminalForms) {
            if (form.cls != cls || form.attributive != text) continue;
            bool endsWithForm = false;
            for (int i = 0; i < form.lastCount; i++) endsWithForm = endsWithForm || LastChar(ja) == form.last[i];
            if (!endsWithForm) continue;
            const Item* next = it->next;
            bool comma = false;
            while (IsOneOfCommas(next) || IsOneOfClosing(next)) {
                comma = comma || IsOneOfCommas(next);
                next = next->next;
            }
            if (next != nullptr && !IsOneOfSentenceEnds(next) &&
                (!EndsClause(next) || (cls == '7' && !comma && !it->group->surface.ends_with(kJaNa) &&
                                       At(next->Code(next->selected), 0) != '7'))) {
                break;
            }
            for (int slot = 0; slot < it->DirectCount(); slot++) {
                if (it->detail->enabled[slot] && it->DirectText(slot) == form.terminal) {
                    it->SetDirectIndex(slot);
                    break;
                }
            }
            break;
        }
    }
}

}  // namespace hdTrans::word
