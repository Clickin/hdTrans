// Text-path passes on the て form that joins two predicates: the sequence 고 of an action verb,
// and the て of an utterance-final verb read as a request.
#include <string_view>

#include "hdTrans/output/families.h"
#include "hdTrans/word/flush.h"
#include "hdTrans/word/strings.h"
#include "hdTrans/word/util.h"

namespace hdTrans::word {
namespace {

// Korean verbs of actions done one after the other.
constexpr std::string_view kSequenceVerbs[] = {
    "먹다", "마시다", "읽다", "씻다", "듣다", "닦다", "입다", "벗다", "신다", "끄다", "닫다", "끝내다", "마치다",
    "노래하다", "놀다", "자다", "쉬다", "샤워하다", "목욕하다", "세수하다", "양치질하다", "공부하다", "일하다",
    "청소하다", "요리하다", "식사하다", "춤추다", "숙제하다", "운동하다", "산책하다",
};

// The item's text as typed, from its start over its length.
std::string_view ItemText(const Item* it) { return Slice(it->text, it->start, it->length); }

// Whether a verb follows it in its sentence (not an auxiliary of its て form), with only words
// and particles between.
bool PredicateFollows(const Item* it) {
    const Item* n = it->next;
    if (n == nullptr) return false;
    constexpr std::string_view kStops[] = {"ゆ", "こ", "ね"};
    const std::string_view next = n->group->surface;
    for (const std::string_view a : kStops) {
        if (next.starts_with(a)) return false;
    }
    for (const std::string_view a : kTeAuxiliaries) {
        if (next.starts_with(a)) return false;
    }
    for (; n != nullptr; n = n->next) {
        if (n->detail->Yougo()) return n->detail->cls == kDetailVerb;
        const std::string_view s = n->group->surface;
        if (s.empty() || AnyRuneIn(s, "、。！？!?…‥「」『』（）()")) return false;
    }
    return false;
}

}  // namespace

bool UtteranceEnd(const Worker& w, const Item* it) {
    std::string_view rest = From(it->text, it->start + it->length);
    if (rest.empty() && it->tokenIndex + 1 < w.TokenCount()) {
        if (w.Span(it->tokenIndex + 1) != 0) return false;
        rest = w.Tok(it->tokenIndex + 1).text;
    }
    return TrimRunes(rest, "…‥」』）)。．、ーっッ～").empty();
}

void TeSequence(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0 || it->detail->cls != kDetailVerb) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        const std::string_view d = ItemText(it);
        if (e->dst != "6" || (!d.ends_with("て") && !d.ends_with("で")) || d.ends_with("ないで") ||
            !InSet(kSequenceVerbs, output::Lemma(it->EntryText(it->selected), f.families)) || !PredicateFollows(it)) {
            continue;
        }
        e->dst = "1고";
    }
}

void TeRequest(Flusher& f) {
    for (Item* it = f.w.chain.head; it != nullptr; it = it->next) {
        if (!it->detail->Yougo() || it->DirectIndex() < 0) continue;
        Entry* e = it->DirectEntry(it->DirectIndex());
        if (e == nullptr) continue;
        const std::string_view surface = ItemText(it);
        if (output::Lemma(e->dst, f.families) != "2며" || (!surface.ends_with("て") && !surface.ends_with("で")) ||
            it->EntryText(it->selected).starts_with("있") || !UtteranceEnd(f.w, it)) {
            continue;
        }
        const std::string_view s = it->group->surface;
        e->dst = s.ends_with("しま") || s.ends_with("ちゃ") ? "6서" : "6";
    }
}

}  // namespace hdTrans::word
