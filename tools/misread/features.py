"""Misread decisions and their features from the MI, MC and MS stage records (hdtrans -stages).

The decisions and features are those of src/core/word/misread.cpp; check.py compares the two.
Standard library only.
"""
import os

SMALL_TSU = ('っ', 'ッ')
MAX_ALTERNATIVES = 6


def readable(t):
    return t == '' or any('가' <= c <= '힣' for c in t)
REPLACEABLE = ('None', 'FirstXxx', 'Digit', 'Single', 'LocalRule', 'GlobalRule')


def unesc(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == '\\' and i + 1 < len(s):
            n = s[i + 1]
            if n == 'x' and i + 3 < len(s):
                out.append(chr(int(s[i + 2:i + 4], 16)))
                i += 4
                continue
            out.append({'t': '\t', 'n': '\n', 'r': '\r', '\\': '\\'}.get(n, n))
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def records(stage_dir, tag):
    """{line: [fields]} of input.TAG.got.tsv."""
    out, line = {}, None
    path = os.path.join(stage_dir, f'input.{tag}.got.tsv')
    if not os.path.exists(path):
        return out
    for raw in open(path, encoding='utf-8', errors='surrogateescape'):
        f = raw.rstrip('\n').split('\t')
        if f[0] == '#LINE':
            line = int(f[1])
            out.setdefault(line, [])
        else:
            out[line].append(f)
    return out


def char_class(c):
    if not c:
        return '_'
    r = ord(c[0])
    if 0x3041 <= r <= 0x3096 or c in 'ゝゞ':
        return 'H'
    if 0x30A1 <= r <= 0x30FA or c in 'ーヽヾ':
        return 'K'
    if 0x3400 <= r <= 0x4DBF or 0x4E00 <= r <= 0x9FFF or 0xF900 <= r <= 0xFAFF or 0x20000 <= r <= 0x3FFFF or c in '々〆':
        return 'C'
    if '0' <= c <= '9' or '０' <= c <= '９':
        return 'D'
    if 'A' <= c <= 'Z' or 'a' <= c <= 'z' or 'Ａ' <= c <= 'Ｚ' or 'ａ' <= c <= 'ｚ':
        return 'L'
    return 'P'


def script_of(s):
    script = '_'
    for c in s:
        k = char_class(c)
        if script != '_' and k != script:
            return 'M'
        script = k
    return script


def char_value(c):
    k = char_class(c)
    return c if k == 'P' else k


def runes_value(s):
    return str(len(s)) if len(s) < 4 else '4+'


def rank_value(slot):
    return 's' if slot < 0 else (str(slot) if slot < 2 else '2+')


def count_value(n):
    return str(n) if n < 3 else '3+'


class Item:
    def __init__(self, f):
        self.chunk, self.sent, self.index = int(f[1]), int(f[2]), int(f[3])
        self.at = f[4]
        self.length = int(f[5])
        self.surface, self.before, self.after = unesc(f[6]), unesc(f[7]), unesc(f[8])
        self.line_start = f[9] == '1'
        self.detail = int(f[10])
        self.fixed = f[11] == '1'
        self.selected = int(f[12])
        self.provenance = f[13]
        self.direct_surface = unesc(f[14])
        self.direct_index = int(f[15])
        self.direct_provenance = f[16]
        self.words, self.endings = [], []  # (part, sem, gram, enabled, target, dictionary form)
        self.sound = ''
        self.prev = self.next = None

    def entry(self):
        return self.words[self.selected] if 0 <= self.selected < len(self.words) else None

    def digit(self):
        e = self.entry()
        return e[0][1:2] if e else ''


def chains(stage_dir):
    """{line: [[Item of one sentence, in chain order]]}."""
    items = {}
    for line, recs in records(stage_dir, 'MI').items():
        by = {}
        for f in recs:
            it = Item(f)
            by[(it.chunk, it.sent, it.index)] = it
        items[line] = by
    for tag in ('MC', 'MS'):
        for line, recs in records(stage_dir, tag).items():
            for f in recs:
                it = items[line][(int(f[1]), int(f[2]), int(f[3]))]
                if tag == 'MS':
                    it.sound = unesc(f[4])
                else:
                    e = (unesc(f[6]), unesc(f[7]), unesc(f[8]), f[9] == '1', unesc(f[10]), unesc(f[11]) if len(f) > 11 else '')
                    (it.words if f[4] == 'W' else it.endings).append(e)
    out = {}
    for line, by in items.items():
        sentences = {}
        for key in sorted(by):
            sentences.setdefault(key[:2], []).append(by[key])
        for chain in sentences.values():
            for a, b in zip(chain, chain[1:]):
                a.next, b.prev = b, a
        out[line] = [sentences[k] for k in sorted(sentences)]
    return out


def nearest_word(it, forward, skip):
    n = it.next if forward else it.prev
    while n is not None:
        d = n.digit()
        if script_of(n.surface) != 'P' and not (d != '' and d in skip):
            return n
        n = n.next if forward else n.prev
    return None


def surface_of(it):
    return '_' if it is None else it.surface


def neighbour_part(it):
    if it is None:
        return '_'
    e = it.entry()
    return '_' if e is None else e[0][:2]


class Decision:
    def __init__(self, item, kind):
        self.item, self.kind = item, kind
        self.candidates = []  # [slot, target, chosen, features, score]


def decisions(chain):
    out = []
    for it in chain:
        if it.fixed:
            continue
        e = it.entry()
        if e is not None and readable(e[4]):
            d = Decision(it, 'W')
            d.candidates.append([it.selected, e[4], True])
            digit = e[0][1:2]
            for i, w in enumerate(it.words):
                if len(d.candidates) > MAX_ALTERNATIVES:
                    break
                if i == it.selected or w[0][1:2] != digit or not readable(w[4]) or any(c[1] == w[4] for c in d.candidates):
                    continue
                d.candidates.append([i, w[4], False])
            if it.sound and not any(c[1] == it.sound for c in d.candidates):
                d.candidates.append([-1, it.sound, False])
            if len(d.candidates) >= 2:
                out.append(d)
        di = it.direct_index
        if 0 <= di < len(it.endings) and it.direct_provenance != 'Fixed' and readable(it.endings[di][4]):
            d = Decision(it, 'E')
            d.candidates.append([di, it.endings[di][4], True])
            for i, w in enumerate(it.endings):
                if len(d.candidates) > MAX_ALTERNATIVES:
                    break
                if i == di or not w[3] or not readable(w[4]) or any(c[1] == w[4] for c in d.candidates):
                    continue
                d.candidates.append([i, w[4], False])
            if len(d.candidates) >= 2:
                out.append(d)
    for d in out:
        shared = item_features(d)
        for c in d.candidates:
            c.append(shared + candidate_features(d, c))
    return out


def item_features(d):
    it = d.item
    s = it.surface
    before, after = char_value(it.before), char_value(it.after)
    f = ['k=' + d.kind, 's=' + s, 'sc=' + script_of(s), 'n=' + runes_value(s), 'b=' + before, 'a=' + after,
         f's|b={s}|{before}', f's|a={s}|{after}']
    if it.line_start:
        f.append('ls')
    first, last = char_class(s[:1]), char_class(s[-1:])
    if first in 'HK' and char_class(it.before) == first:
        f.append('split=b')
    if last in 'HK' and char_class(it.after) == last:
        f.append('split=a')
    if it.before in SMALL_TSU:
        f.append('tsu=b')
    if it.after in SMALL_TSU:
        f.append('tsu=a')
    if s[-1:] in SMALL_TSU and s:
        f.append('tsu=e')
    f += ['pp=' + neighbour_part(it.prev), 'np=' + neighbour_part(it.next),
          'ps=' + surface_of(it.prev), 'ns=' + surface_of(it.next),
          'pw=' + surface_of(nearest_word(it, False, '')), 'nw=' + surface_of(nearest_word(it, True, ''))]
    pk, nk = surface_of(nearest_word(it, False, '25')), surface_of(nearest_word(it, True, '25'))
    f += ['pk=' + pk, 'nk=' + nk, f's|pk={s}|{pk}', f's|nk={s}|{nk}', 'm=' + count_value(len(d.candidates) - 1)]
    if d.kind == 'E':
        f.append('es=' + it.direct_surface)
    return f


def candidate_features(d, c):
    it = d.item
    slot, target, chosen = c[0], c[1], c[2]
    surface = it.surface if d.kind == 'W' else it.direct_surface
    provenance = (it.provenance if d.kind == 'W' else it.direct_provenance) if chosen else 'Alt'
    e = None if slot < 0 else (it.words if d.kind == 'W' else it.endings)[slot]
    part = 'snd' if e is None else e[0]
    part2 = part[:2]
    f = ['pv=' + provenance, 'pos=' + part, 'pos2=' + part2, f'pv|pos2={provenance}|{part2}',
         f'pos2|b={part2}|{char_value(it.before)}', f'pos2|a={part2}|{char_value(it.after)}']
    if it.line_start:
        f.append('pos2|ls=' + part2)
    f += ['r=' + rank_value(slot), 't=' + target, f'st={surface}|{target}']
    if e is None:
        return f + ['snd']
    sem, gram = e[1], e[2]
    f += ['sem=' + sem[i:i + 3] for i in range(0, len(sem) - 2, 3)]
    f += ['g=' + gram[i:i + 2] for i in range(0, len(gram) - 1, 2)]
    if part[1:2] in ('2', '5'):
        f.append('fn')
    return f


def load_model(path):
    """(weights {feature: weight}, bias, threshold, keep) of a model TSV."""
    weights, meta = {}, {}
    for raw in open(path, encoding='utf-8'):
        f = raw.rstrip('\n').split('\t')
        if f[0].startswith('#') or len(f) < 2:
            continue
        if f[0] in ('@bias', '@threshold', '@keep'):
            meta[f[0][1:]] = int(f[1])
        else:
            weights[unesc(f[0])] = int(f[1])
    return weights, meta.get('bias', 0), meta.get('threshold', 1 << 30), meta.get('keep', 0)


def score(model, features):
    weights, bias = model[0], model[1]
    return max(-(1 << 31), min((1 << 31) - 1, bias + sum(weights.get(f, 0) for f in features)))


def replacement(d, model):
    """The candidate that replaces the chosen one of d, or None (MisreadReplacement)."""
    chosen = d.candidates[0]
    provenance = d.item.provenance if d.kind == 'W' else d.item.direct_provenance
    if chosen[4] < model[2] or provenance not in REPLACEABLE:
        return None
    best = None
    for c in d.candidates[1:]:
        if c[4] < model[3] and (best is None or c[4] < best[4]):
            best = c
    return best
