#include <cstdint>
#include <algorithm>
#include <array>

#include "core/dict/snapshot.h"

namespace hdTrans::dict {

namespace {

// The queries of modes 1..3 for empty text.
constexpr std::array<std::string_view, 3> kSentinelKeys = {"D", "I", "V"};
constexpr int kBucketCount = 26;  // tags 'A'..'Z'
constexpr int kNil = -1;

bool EndingTag(char tag) { return tag == 'S' || tag == 'T' || tag == 'U'; }

// The ordering weight of an M (conjugation ending) candidate, keyed by its kind;
// higher sorts first.
int GobiInfValue(int8_t kind) {
    switch (kind) {
    case '9': case ':': return 0x63;
    case ';': case '=': return 0x62;
    case '0': return 0x61;
    case '1': return 0x60;
    case '2': return 0x5F;
    case '3': return 0x5E;
    case '4': return 0x5D;
    case '5': return 0x5C;
    case '6': return 0x5B;
    case '8': return 0x5A;
    case '<': return 0x59;
    case '7': return 0x58;
    }
    return 0x64;
}

// Candidates linked through child indexes while a lookup orders them.
struct Pool {
    std::vector<Candidate> c;
    std::vector<int> child;

    int Add(const Candidate& x) {
        c.push_back(x);
        child.push_back(kNil);
        return static_cast<int>(c.size()) - 1;
    }
    int8_t Tag(int i) const { return c[static_cast<std::size_t>(i)].codes->part.At(0); }
    std::size_t SourceLen(int i) const { return c[static_cast<std::size_t>(i)].source.size(); }
    int& Child(int i) { return child[static_cast<std::size_t>(i)]; }
    int Tail(int i) {
        while (Child(i) != kNil) i = Child(i);
        return i;
    }
};

// Places c before the first candidate with a higher tag or the same tag and a
// shorter source.
int InsertUserCandidate(Pool& p, int list, int c) {
    const int8_t tag = p.Tag(c);
    if (list == kNil || p.Tag(list) > tag || (p.Tag(list) == tag && p.SourceLen(list) < p.SourceLen(c))) {
        p.Child(c) = list;
        return c;
    }
    int at = list;
    while (p.Child(at) != kNil) {
        const int next = p.Child(at);
        const int8_t t = p.Tag(next);
        if (t > tag || (t == tag && p.SourceLen(next) < p.SourceLen(c))) break;
        at = next;
    }
    p.Child(c) = p.Child(at);
    p.Child(at) = c;
    return list;
}

// Merges incoming into current by ascending tag, then longer source first. On equal
// tag and length the candidate from the prior dictionary wins; the current candidate
// gets "XXX" appended to its semantic codes when it loses to it, or when it is
// itself from the prior dictionary.
int MergeCandidates(Pool& p, int current, int incoming, std::string_view prior) {
    int head = kNil, last = kNil;
    auto take = [&](int x) {
        if (head == kNil) head = x;
        else p.Child(last) = x;
        last = x;
    };
    auto markXXX = [&](int x) {
        Candidate& c = p.c[static_cast<std::size_t>(x)];
        c.codes = c.codes->withXXX;
    };
    while (current != kNil && incoming != kNil) {
        const int8_t c = p.Tag(current), i = p.Tag(incoming);
        const std::size_t cl = p.SourceLen(current), il = p.SourceLen(incoming);
        const Candidate& cur = p.c[static_cast<std::size_t>(current)];
        const Candidate& inc = p.c[static_cast<std::size_t>(incoming)];
        if (c < i || (c == i && cl > il)) {
            take(current);
            current = p.Child(current);
        } else if (c > i || cl < il) {
            take(incoming);
            incoming = p.Child(incoming);
        } else if (cur.codes->dict == prior) {
            markXXX(current);
            take(current);
            current = p.Child(current);
        } else if (inc.codes->dict == prior) {
            markXXX(current);
            take(incoming);
            incoming = p.Child(incoming);
        } else {
            take(current);
            current = p.Child(current);
        }
    }
    const int rest = current != kNil ? current : incoming;
    if (rest != kNil) take(rest);
    return head;
}

}  // namespace

Candidates Snapshot::GetWordInfo(std::string_view text, LookupMode mode, const LookupContext& ctx) const {
    Candidates out;
    if (Healthy()) out = GetWordInfoImpl(text, mode, ctx);
    if (Healthy()) return out;
    const auto fallback = Fallback();
    return fallback ? fallback->GetWordInfo(text, mode, ctx) : Candidates{};
}

Candidates Snapshot::GetWordInfoImpl(std::string_view text, LookupMode mode, const LookupContext& ctx) const {
    Pool pool;
    Candidates result;
    // User-dictionary matches.
    int out = kNil;
    int userCount = 0;
    user_.Prefixes(text, [&](int k, std::size_t n) {
        user_.EachRecord(k, [&](const Record& r) {
            userCount++;
            Candidate c;
            c.source = text.substr(0, n);
            c.target = r.target;
            c.codes = r.codes;
            out = InsertUserCandidate(pool, out, pool.Add(c));
            return true;
        });
    });

    std::string_view query = text;
    const int m = static_cast<int>(mode);
    if (text.empty() && m >= 1 && m <= 3) query = kSentinelKeys[static_cast<std::size_t>(m - 1)];
    std::array<int, 2 * kBucketCount> lists;
    lists.fill(kNil);
    int count = 0;
    base_.Prefixes(query, [&](int k, std::size_t n) {
        std::string_view source = query.substr(0, n);
        for (const std::string_view sk : kSentinelKeys) {
            if (source == sk) source = {};
        }
        const Respelling* sp = Place(source);
        int mList = kNil;
        std::array<int, kBucketCount> insertAfter;
        insertAfter.fill(kNil);
        bool ended = false;
        base_.EachRecord(k, [&](const Record& r) {
            if (r.codes->dict.SourceOnly()) return true;
            const PartCode major = r.codes->part;
            if (!major.LetterTag()) {
                ended = true;
                return false;
            }
            const char tag = major.Tag();
            if (!AcceptsTag(mode, tag)) return true;
            Candidate c;
            c.source = source;
            c.target = Respelled(sp, ctx.persons, source, r);
            c.codes = r.codes;
            c.rule = r.rule;
            c.id = r.id;
            const int data = pool.Add(c);
            const int bucket = tag - 'A';
            if (tag == 'M') {
                const int weight = GobiInfValue(major.At(3));
                if (mList == kNil || weight > GobiInfValue(pool.c[static_cast<std::size_t>(mList)].codes->part.At(3))) {
                    pool.Child(data) = mList;
                    mList = data;
                } else {
                    int scan = mList;
                    while (pool.Child(scan) != kNil &&
                           weight <= GobiInfValue(pool.c[static_cast<std::size_t>(pool.Child(scan))].codes->part.At(3))) {
                        scan = pool.Child(scan);
                    }
                    pool.Child(data) = pool.Child(scan);
                    pool.Child(scan) = data;
                }
            } else if (!ctx.priorDict.empty() && r.codes->dict == ctx.priorDict) {
                pool.Child(data) = lists[static_cast<std::size_t>(kBucketCount + bucket)];
                lists[static_cast<std::size_t>(kBucketCount + bucket)] = data;
            } else if (insertAfter[static_cast<std::size_t>(bucket)] != kNil) {
                int& after = insertAfter[static_cast<std::size_t>(bucket)];
                pool.Child(data) = pool.Child(after);
                pool.Child(after) = data;
                after = data;
            } else {
                insertAfter[static_cast<std::size_t>(bucket)] = data;
                pool.Child(data) = lists[static_cast<std::size_t>(bucket)];
                lists[static_cast<std::size_t>(bucket)] = data;
            }
            count++;
            return true;
        });
        if (!ended && mList != kNil) {
            constexpr std::size_t kM = 'M' - 'A';
            pool.Child(pool.Tail(mList)) = lists[kM];
            lists[kM] = mList;
        }
    });

    if (mode == LookupMode::General) {
        // tail starts at the head of the user matches, so a prior bucket replaces every
        // user match after the first.
        int tail = out;
        for (int b = kBucketCount; b < 2 * kBucketCount; b++) {
            const int l = lists[static_cast<std::size_t>(b)];
            if (l == kNil) continue;
            if (out == kNil) out = l;
            else pool.Child(tail) = l;
            tail = pool.Tail(l);
        }
        for (int b = 0; b < kBucketCount; b++) {
            const int l = lists[static_cast<std::size_t>(b)];
            if (l == kNil) continue;
            out = out == kNil ? l : MergeCandidates(pool, out, l, ctx.priorDict);
        }
    } else {
        const std::size_t tag = static_cast<std::size_t>("UTSV"[m - 1] - 'A');
        if (lists[tag] != kNil) out = lists[tag];
        else if (lists[kBucketCount + tag] != kNil) out = lists[kBucketCount + tag];
    }
    result.count = userCount + count;

    // Plain and name entries.
    std::vector<const UserEntry*> owned;
    if (ctx.plain) owned = ctx.plain->Search(text, mode);
    NameEntries(text, mode, ctx, owned, result.names);
    if (!result.names.empty()) {
        // Each name entry (longer keys first) goes before the first plain entry with a
        // shorter key.
        std::vector<const UserEntry*> merged;
        merged.reserve(owned.size() + result.names.size());
        std::size_t i = 0;
        for (const UserEntry& e : result.names) {
            while (i < owned.size() && owned[i]->key.size() >= e.key.size()) merged.push_back(owned[i++]);
            merged.push_back(&e);
        }
        merged.insert(merged.end(), owned.begin() + static_cast<std::ptrdiff_t>(i), owned.end());
        owned = std::move(merged);
    }
    if (ctx.skip.fn) std::erase_if(owned, [&](const UserEntry* e) { return ctx.skip(*e); });

    std::vector<Candidate>& list = result.list;
    if (owned.empty()) {
        for (int c = out; c != kNil; c = pool.Child(c)) list.push_back(pool.c[static_cast<std::size_t>(c)]);
        return result;
    }

    // A base candidate with the key and class digit of an owned entry is dropped (an
    // ending only with its subclass as well), except for end rows: the base
    // candidates stay behind them. Force entries come first among the owned ones;
    // rank[i] is the number of owned entries placed before kept[i].
    std::stable_sort(owned.begin(), owned.end(), [](const UserEntry* a, const UserEntry* b) { return a->force && !b->force; });
    std::vector<int> kept, rank;
    int dropped = 0;
    for (int r = out; r != kNil; r = pool.Child(r)) {
        const Candidate& c = pool.c[static_cast<std::size_t>(r)];
        const PartCode rp = c.codes->part;
        bool shadowed = false;
        int n = 0;
        for (const UserEntry* e : owned) {
            const PartCode ep = e->Part();
            if (e->key == c.source && ep.SameClass(rp) && !e->end && (!EndingTag(ep.Tag()) || ep.SameSub(rp))) {
                shadowed = true;
            }
            if (ctx.strategy == Strategy::UserFirst || e->force || e->key.size() >= c.source.size()) n++;
        }
        if (shadowed) {
            dropped++;
            continue;
        }
        kept.push_back(r);
        rank.push_back(n);
    }
    for (std::size_t k = 0; k <= owned.size(); k++) {
        for (std::size_t i = 0; i < kept.size(); i++) {
            if (static_cast<std::size_t>(rank[i]) == k) list.push_back(pool.c[static_cast<std::size_t>(kept[i])]);
        }
        if (k < owned.size()) {
            const UserEntry* e = owned[k];
            Candidate c;
            c.source = e->key;
            c.target = e->target;
            c.codes = e->codes;
            c.user = e;
            list.push_back(c);
        }
    }
    result.count += static_cast<int>(owned.size()) - dropped;
    return result;
}

std::vector<Candidate> Snapshot::RetrieveRecords(std::string_view key, LookupMode mode) const {
    std::vector<Candidate> out;
    if (Healthy()) out = RetrieveRecordsImpl(key, mode);
    if (Healthy()) return out;
    const auto fallback = Fallback();
    return fallback ? fallback->RetrieveRecords(key, mode) : std::vector<Candidate>{};
}

std::vector<Candidate> Snapshot::RetrieveRecordsImpl(std::string_view key, LookupMode mode) const {
    const int m = static_cast<int>(mode);
    if (key.empty() && m >= 1 && m <= 3) key = kSentinelKeys[static_cast<std::size_t>(m - 1)];
    std::vector<Candidate> out;
    const int k = base_.Find(key);
    if (k < 0) return out;
    base_.EachRecord(k, [&](const Record& r) {
        Candidate c;
        c.source = key;
        c.target = r.target;
        c.codes = r.codes;
        c.rule = r.rule;
        c.id = r.id;
        out.push_back(c);
        return true;
    });
    return out;
}

std::size_t Snapshot::MemoryBytes() const {
    std::size_t n = sizeof(Snapshot) + codes_.MemoryBytes() + base_.MemoryBytes() + user_.MemoryBytes() +
                    names_.MemoryBytes();
    for (const auto& [key, sp] : places_) n += 64 + key.capacity() + sp.ko.capacity() + sp.person.capacity();
    for (const File& f : plain_) n += sizeof(File) + f.name.capacity() + f.text.capacity();
    for (const UserText* u : {rawPlain_.get(), textPlain_.get(), rawExtraPlain_.get(), textExtraPlain_.get()}) {
        if (!u) continue;
        for (const UserEntry& e : u->Entries()) {
            n += sizeof(UserEntry) + 2 * sizeof(void*) + e.key.capacity() + e.target.capacity() + e.negative.capacity();
        }
    }
    for (const auto& [key, list] : senses_) {
        n += 64 + key.capacity();
        for (const userdict::Sense& x : list) {
            n += sizeof(x) + x.key.capacity() + x.target.capacity();
            for (const std::string& t : x.triggers) n += sizeof(t) + t.capacity();
        }
    }
    return n + families_.MemoryBytes();
}

}  // namespace hdTrans::dict
