"""cg.py: the CG-lite rule interpreter of src/core/word/cg.cpp on the rows of label.py, a learner of
ordered REMOVE-chosen rules, and their evaluation against the labels.

  cg.py eval RULES[,RULES...] ROWS.tsv [-sets dev,test]   changes, their precision and fixes
  cg.py learn TRAIN.tsv[,CONSENSUS.tsv] OUT.tsv [-min-net 3] [-min-precision 0.8] [-min-works 1]

A rule table has lines "tier action target condition note" (tab-separated, # comments). action is
REMOVE or SELECT; target and condition are space-separated literals: a feature of the decision
(misread.h), "feature*" for a prefix, "!literal" for its negation, "chosen" for the candidate the
engine chose; "name={a,b}" for either value (a trailing * still marks a prefix). Target literals are tested on each live candidate, condition literals on the chosen
candidate; a condition literal "kept" holds while the chosen candidate is live, "any:L" / "none:L"
when some / no live candidate other than the chosen one matches L. "\\s" in a literal is a space.
Tiers run in ascending order, each to a fixpoint; REMOVE never removes every live candidate and
SELECT applies only when some but not all live candidates match. When the chosen candidate is
removed, the first live candidate (candidate order) replaces it; the chosen candidate's provenance
must be replaceable (misread.h).

learn reads labelled rows; with a second (consensus) file, a decision it labels takes that label (and
is left out when the judges disagree). It greedily picks rules "SELECT !chosen [alt] if kept ctx" with
ctx one or two literals of the chosen candidate from small templates, alt one literal of the judge's
alternative or none, by net = fixed - broken on set train, until net or precision (fixed / changed
among labelled decisions) falls below the limits, or the fixed decisions come from fewer works.
"""
import argparse, collections, sys

REPLACEABLE = {'pv=None', 'pv=FirstXxx', 'pv=Digit', 'pv=Single', 'pv=LocalRule', 'pv=GlobalRule'}


def unesc(x):
    return x.replace('\\t', '\t').replace('\\\\', '\\')


class Decision:
    __slots__ = ('key', 'cands', 'label', 'preferred')

    def __init__(self, key):
        self.key, self.cands, self.label, self.preferred = key, [], -1, set()


def load(path):
    """{key: Decision} with cands [(slot, feature set)], chosen first."""
    out = {}
    for raw in open(path, encoding='utf-8'):
        f = raw.rstrip('\n').split('\t')
        key = (f[0], f[1], f[2], f[3], f[4])
        d = out.get(key)
        if d is None:
            d = out[key] = Decision(key)
        i = len(d.cands)
        d.cands.append((int(f[5]), frozenset(unesc(x) for x in f[11:])))
        if f[6] == '1':
            d.label = int(f[7])
        elif f[8] == '1':
            d.preferred.add(i)
    return out


def literal(text):
    """(negated, [(prefix, text)]): the alternatives of "name={a,b}" expand to name=a and name=b."""
    neg = text.startswith('!')
    t = (text[1:] if neg else text).replace('\\s', ' ')
    alts = [t]
    if '{' in t and t.find('}') > t.find('{'):
        i, j = t.index('{'), t.index('}')
        alts = [t[:i] + a + t[j + 1:] for a in t[i + 1:j].split(',')]
    return (neg, [(a.endswith('*'), a[:-1] if a.endswith('*') else a) for a in alts])


def holds(lit, feats, is_chosen):
    neg, alts = lit
    v = False
    for prefix, t in alts:
        if t == 'chosen':
            v = is_chosen
        elif prefix:
            v = any(f.startswith(t) for f in feats)
        else:
            v = t in feats
        if v:
            break
    return v != neg


def read_rules(paths):
    rules = []
    for path in paths.split(','):
        for raw in open(path, encoding='utf-8'):
            if raw.startswith('#') or not raw.strip():
                continue
            f = raw.rstrip('\n').split('\t') + ['', '', '']
            target = [literal(x) for x in f[2].split()]
            cond = []
            for x in f[3].split():
                if x == 'kept':
                    cond.append(('kept', None))
                elif x.startswith(('any:', 'none:')):
                    k, v = x.split(':', 1)
                    cond.append((k, literal(v)))
                else:
                    cond.append(('c', literal(x)))
            rules.append((int(f[0]), f[1], target, cond, f[4]))
    rules.sort(key=lambda r: r[0])
    return rules


def apply(rules, cands):
    """The index of the candidate that replaces the chosen one, None to keep it, and the notes of the
    rules that removed candidates."""
    if not cands[0][1] & REPLACEABLE:
        return None, []
    live = list(range(len(cands)))
    fired = []
    tiers = sorted({r[0] for r in rules})
    for tier in tiers:
        changed = True
        while changed and len(live) > 1:
            changed = False
            for t, action, target, cond, note in rules:
                if t != tier or len(live) < 2:
                    continue
                ok = True
                for kind, lit in cond:
                    if kind == 'kept':
                        ok = 0 in live
                    elif kind == 'c':
                        ok = holds(lit, cands[0][1], True)
                    else:
                        hit = any(holds(lit, cands[i][1], False) for i in live if i)
                        ok = hit if kind == 'any' else not hit
                    if not ok:
                        break
                if not ok:
                    continue
                match = [i for i in live if all(holds(l, cands[i][1], i == 0) for l in target)]
                if not match or len(match) == len(live):
                    continue
                live = [i for i in live if i not in match] if action == 'REMOVE' else match
                fired.append(note)
                changed = True
    return (None if 0 in live else live[0]), fired


def evaluate(rules, decs, sets, show=0):
    n = changed = labelled = bad = fixed = 0
    per_rule = collections.defaultdict(lambda: [0, 0, 0])
    examples = []
    for d in decs.values():
        if d.key[0] not in sets:
            continue
        n += 1
        alt, fired = apply(rules, d.cands)
        if alt is None:
            continue
        changed += 1
        note = fired[-1] if fired else ''
        pr = per_rule[note]
        pr[0] += 1
        if d.label >= 0:
            labelled += 1
            bad += d.label
            pr[1] += d.label
            if d.label == 1 and alt in d.preferred:
                fixed += 1
                pr[2] += 1
        if len(examples) < show:
            examples.append((d, alt, note))
    return dict(decisions=n, changed=changed, labelled=labelled, bad=bad, fixed=fixed,
                precision=bad / labelled if labelled else float('nan'),
                fix_rate=fixed / labelled if labelled else float('nan')), per_rule, examples


def feat(cands, i, prefix):
    return [f for f in cands[i][1] if f.startswith(prefix)]


# Context literals of the chosen candidate: identity, neighbour surface, neighbour code, script class
# and line position.
IDENT = ('st=', 's=', 'es=', 'pos2=')
CONTEXT = ('ps=', 'ns=', 'pp=', 'np=', 'b=', 'a=', 'sc=', 'ls', 'tsu=', 'split=', 'pv=', 'k=')
ALT = ('snd', 'pos2=', 't=')


def ctx_keys(feats):
    ident = sorted(f for f in feats if f.startswith(IDENT))
    ctx = sorted(f for f in feats if f.startswith(CONTEXT) and not f.startswith('pv|'))
    keys = [(i,) for i in ident]
    keys += [(i, c) for i in ident for c in ctx]
    small = [c for c in ctx if c.startswith(('b=', 'a=', 'pp=', 'np=', 'ls', 'tsu=', 'split=', 'sc='))]
    keys += [(a, b) for k, a in enumerate(small) for b in small[k + 1:]]
    return keys


def alt_options(d):
    """{alt literal: index of the first alternative matching it} ('' for the first alternative)."""
    out = {'': 1}
    for i in range(1, len(d.cands)):
        for f in d.cands[i][1]:
            if f == 'snd' or f.startswith(('pos2=', 't=')):
                out.setdefault(f, i)
    return out


def work(chapter):
    """The work of a chapter name: n0332hu-1, rec-n0332hu and oos-n0332hu-3 are n0332hu."""
    for p in ('rec-', 'oos-', 'new-'):
        if chapter.startswith(p):
            chapter = chapter[len(p):]
    return chapter.split('-')[0]


def learn(train_paths, out_path, min_net, min_precision, min_works=1):
    paths = train_paths.split(',')
    decs = {k: d for k, d in load(paths[0]).items() if k[0] == 'train'}
    if len(paths) > 1:
        for k, c in load(paths[1]).items():
            if k in decs:
                if c.label < 0:
                    del decs[k]
                else:
                    decs[k].label, decs[k].preferred = c.label, c.preferred
    pool = [d for d in decs.values() if d.label >= 0 and d.cands[0][1] & REPLACEABLE]
    print(f'train decisions {len(pool)}, wrong {sum(d.label for d in pool)}', file=sys.stderr)
    # Context keys with at least min_net fixable decisions.
    fixable = collections.Counter()
    for d in pool:
        if d.label == 1 and d.preferred:
            opts = alt_options(d)
            alts = {a for a, i in opts.items() if i in d.preferred}
            for k in ctx_keys(d.cands[0][1]):
                for a in alts:
                    fixable[(k, a)] += 1
    keep = {r for r, n in fixable.items() if n >= min_net}
    keep_ctx = {k for k, _ in keep}
    print(f'candidate rules {len(keep)}', file=sys.stderr)
    contrib = []  # per decision: [(rule, outcome)] with outcome +1 fixed, -1 broken, 0 neither
    counts = collections.defaultdict(lambda: [0, 0, 0, collections.Counter()])  # rule -> fixed, broken, changed, works fixed
    for d in pool:
        opts = None
        mine = []
        for k in ctx_keys(d.cands[0][1]):
            if k not in keep_ctx:
                continue
            if opts is None:
                opts = alt_options(d)
            for a, i in opts.items():
                r = (k, a)
                if r not in keep:
                    continue
                o = -1 if d.label == 0 else 1 if i in d.preferred else 0
                mine.append((r, o))
                c = counts[r]
                c[0] += o == 1
                c[1] += o == -1
                c[2] += 1
                if o == 1:
                    c[3][work(d.key[1])] += 1
        contrib.append(mine)
    alive = [True] * len(pool)
    by_rule = collections.defaultdict(list)
    for j, mine in enumerate(contrib):
        for r, _ in mine:
            by_rule[r].append(j)
    rules = []
    while True:
        best = None
        for r, (fx, br, ch, works) in counts.items():
            net = fx - br
            if net < min_net or fx / ch < min_precision or sum(1 for n in works.values() if n > 0) < min_works:
                continue
            score = (net, fx / ch, -len(r[0]) - (r[1] != ''), r)
            if best is None or score > best[0]:
                best = (score, r)
        if best is None:
            break
        r = best[1]
        fx, br, ch, _ = counts[r]
        rules.append((r, fx, br, ch))
        print(f'{len(rules):3d} net {fx - br:3d} fixed {fx} broken {br} changed {ch}  {" ".join(r[0])}  -> {r[1] or "first"}', file=sys.stderr)
        for j in by_rule[r]:
            if not alive[j]:
                continue
            alive[j] = False
            for rr, o in contrib[j]:
                c = counts[rr]
                c[0] -= o == 1
                c[1] -= o == -1
                c[2] -= 1
                if o == 1:
                    c[3][work(pool[j].key[1])] -= 1
    def esc(x):
        return x.replace(' ', '\\s')
    with open(out_path, 'w', encoding='utf-8') as o:
        o.write(f'# learned REMOVE-chosen rules (cg.py learn, net >= {min_net}, precision >= {min_precision}); '
                'fixed/broken/changed on train\n')
        for n, ((ctx, a), fx, br, ch) in enumerate(rules, 1):
            target = '!chosen' + (' ' + esc(a) if a else '')
            o.write(f'10\tSELECT\t{target}\tkept {" ".join(esc(x) for x in ctx)}\tE{n} {fx}/{br}/{ch}\n')


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'learn':
        ap = argparse.ArgumentParser()
        ap.add_argument('cmd')
        ap.add_argument('train')
        ap.add_argument('out')
        ap.add_argument('-min-net', type=int, default=3)
        ap.add_argument('-min-precision', type=float, default=0.8)
        ap.add_argument('-min-works', type=int, default=1)
        a = ap.parse_args()
        learn(a.train, a.out, a.min_net, a.min_precision, a.min_works)
        return 0
    if len(sys.argv) > 1 and sys.argv[1] == 'eval':
        ap = argparse.ArgumentParser()
        ap.add_argument('cmd')
        ap.add_argument('rules')
        ap.add_argument('rows')
        ap.add_argument('-sets', default='dev,test')
        ap.add_argument('-rules-detail', action='store_true')
        a = ap.parse_args()
        rules = read_rules(a.rules)
        decs = load(a.rows)
        m, per_rule, _ = evaluate(rules, decs, set(a.sets.split(',')))
        print('\t'.join(f'{k}={v:.3f}' if isinstance(v, float) else f'{k}={v}' for k, v in m.items()))
        if a.rules_detail:
            for note, (ch, bad, fx) in sorted(per_rule.items(), key=lambda x: -x[1][0]):
                print(f'  {ch:4d} bad {bad:4d} fixed {fx:4d}  {note}')
        return 0
    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main())
