"""label.py ACCEPT.tsv|-decisions LABELS.tsv|-any LABELS.tsv CHAPTERS OUT.tsv: labels the chosen candidates of the
misread decisions of judged lines.

With -decisions, LABELS.tsv holds the per-decision verdicts of judge.py: label is 1 when the chosen
reading is wrong, and preferred marks the alternative the judge named; a decision without a verdict
is left out. Several files, comma-separated, give consensus labels: 1 when every verdict says
wrong, 0 when every one says right, -1 otherwise; preferred needs every verdict to name it. With
-any instead, one verdict saying wrong makes the label 1. Otherwise ACCEPT.tsv holds line verdicts (hdeval judge -accept: id, ja, out, verdict, cause, span, fix; the id is
<chapter>:<line>). CHAPTERS has lines "<set> <chapter> <stage dir>". For each decision of a judged
line one row is written per candidate:

  set chapter line decision kind slot chosen label preferred inspan cause features...

label (chosen candidates only): 1 bad, 0 ok, -1 unknown. An acceptable line makes every chosen
candidate ok. In a bad line, a decision whose item overlaps the judge's span is bad when the Korean
of the chosen candidate does not occur in the fix, ok when it does; outside the span it is ok
(inspan 0). preferred marks an alternative whose Korean occurs in the fix when the chosen one's does
not. Korean is compared without spaces, engine markers (&..@), digits and particle alternations.
"""
import os, re, sys, unicodedata
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import features as fx

MARKER = re.compile(r'&[^@&]*@')


def variants(target):
    """The spellings a Korean candidate may take in a fix: (을)를 gives 를 and 을."""
    t = MARKER.sub('', target)
    t = re.sub(r'[0-9A-Za-z\s]', '', t)
    plain = re.sub(r'\([^)]*\)', '', t)
    inner = re.sub(r'\(([^)]*)\)[^\s(]*', r'\1', t)
    return {v for v in (plain, inner) if v}


def occurs(target, fix):
    return any(v in fix for v in variants(target))


def norm(s):
    return unicodedata.normalize('NFKC', s)


def align(items, ja):
    """{item: (begin, end)} in the NFKC line for the items whose surface is found in order."""
    out, cursor = {}, 0
    for it in items:
        s = norm(it.surface)
        if not s.strip():
            continue
        k = ja.find(s, cursor)
        if k < 0 or k - cursor > 8:
            continue
        out[id(it)] = (k, k + len(s))
        cursor = k + len(s)
    return out


def write_rows(out, set_, chapter, line, n, d, label, preferred, inspan, cause):
    for c in d.candidates:
        row = [set_, chapter, str(line), str(n), d.kind, str(c[0]), '1' if c[2] else '0',
               str(label) if c[2] else '-1', '1' if c[0] in preferred else '0', '1' if inspan else '0', cause]
        out.write('\t'.join(row + [x.replace('\\', '\\\\').replace('\t', '\\t') for x in c[3]]) + '\n')


def from_decisions(labels, chapters, out, any_wrong=False):
    files = labels.split(',')
    votes = {}
    for path in files:
        for raw in open(path, encoding='utf-8'):
            f = raw.rstrip('\n').split('\t')
            votes.setdefault((f[0], int(f[1]), int(f[2])), []).append((f[4] == '0', f[5]))
    verdicts = {}
    for k, v in votes.items():
        if len(v) < len(files):
            continue
        wrong = [w for w, _ in v]
        betters = {b for w, b in v if w}
        if all(wrong) or (any_wrong and any(wrong)):
            verdicts[k] = (True, betters.pop() if len(betters) == 1 and (any_wrong or all(b for _, b in v)) else '')
        elif not any(wrong):
            verdicts[k] = (False, '')
        else:
            verdicts[k] = None
    for raw in open(chapters, encoding='utf-8'):
        if not raw.strip():
            continue
        set_, chapter, stage_dir = raw.split()
        for line, sentences in sorted(fx.chains(stage_dir).items()):
            n = 0
            for chain in sentences:
                for d in fx.decisions(chain):
                    k = (chapter, line, n)
                    if k in verdicts:
                        v = verdicts[k]
                        label = -1 if v is None else 1 if v[0] else 0
                        preferred = {int(v[1])} if v is not None and v[0] and v[1] != '' else set()
                        write_rows(out, set_, chapter, line, n, d, label, preferred, label == 1, '')
                    n += 1


def main(argv):
    if len(argv) == 4 and argv[0] in ('-decisions', '-any'):
        with open(argv[3], 'w', encoding='utf-8') as out:
            from_decisions(argv[1], argv[2], out, argv[0] == '-any')
        return 0
    if len(argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    verdicts = {}
    for raw in open(argv[0], encoding='utf-8'):
        f = raw.rstrip('\n').split('\t')
        if f[0] == 'id' or len(f) < 4:
            continue
        f += [''] * (7 - len(f))
        verdicts[f[0]] = (fx.unesc(f[1]), f[3], f[4], fx.unesc(f[5]), fx.unesc(f[6]))
    with open(argv[2], 'w', encoding='utf-8') as out:
        for raw in open(argv[1], encoding='utf-8'):
            if not raw.strip():
                continue
            set_, chapter, stage_dir = raw.split()
            for line, sentences in sorted(fx.chains(stage_dir).items()):
                v = verdicts.get(f'{chapter}:{line}')
                if v is None or v[1] not in ('ok', 'bad'):
                    continue
                ja, verdict, cause, span, fix = v
                ja = norm(ja)
                items = [it for chain in sentences for it in chain]
                where = align(items, ja)
                span = norm(span)
                s0 = ja.find(span) if span else -1
                fixed = re.sub(r'\s', '', fix)
                n = 0
                for chain in sentences:
                    for d in fx.decisions(chain):
                        pos = where.get(id(d.item))
                        inspan = verdict == 'bad' and s0 >= 0 and pos is not None and pos[0] < s0 + len(span) and s0 < pos[1]
                        label, preferred = 0, set()
                        if inspan:
                            if not occurs(d.candidates[0][1], fixed):
                                preferred = {c[0] for c in d.candidates[1:] if occurs(c[1], fixed)}
                                label = 1 if preferred or len(''.join(variants(d.candidates[0][1]))) >= 2 else -1
                        elif verdict == 'bad' and (s0 < 0 or pos is None):
                            label = -1
                        write_rows(out, set_, chapter, line, n, d, label, preferred, inspan,
                                   cause if verdict == 'bad' else '')
                        n += 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
