"""train.py ROWS.tsv OUT_MODEL.tsv [-kinds W,E] [-precision 0.9]: an L1 logistic regression of
"the chosen candidate is a misreading" on the rows of label.py.

Rows of set train fit the model (chosen candidates with a label, and preferred alternatives as
good readings); set dev picks the regularisation and the threshold, the lowest score at which at
least the given share of the dev decisions scored as bad are bad; set test is only reported.
Weights are written in thousandths of a logit unit. Needs numpy and scikit-learn.
"""
import argparse, collections, sys
import numpy as np
from sklearn.feature_extraction import FeatureHasher
from sklearn.linear_model import LogisticRegression
from scipy import sparse


def read(path, kinds):
    data = collections.defaultdict(list)  # set -> [(features, y, chosen, key)]
    for raw in open(path, encoding='utf-8'):
        f = raw.rstrip('\n').split('\t')
        set_, kind, chosen, label, preferred = f[0], f[4], f[6] == '1', int(f[7]), f[8] == '1'
        if kind not in kinds:
            continue
        key = (f[1], f[2], f[3])
        feats = [x.replace('\\t', '\t').replace('\\\\', '\\') for x in f[11:]]
        if chosen and label >= 0:
            data[set_].append((feats, label, True, key))
        elif not chosen and preferred:
            data[set_].append((feats, 0, False, key))
    return data


def matrix(rows, vocab):
    r, c = [], []
    for i, (feats, *_) in enumerate(rows):
        for f in feats:
            j = vocab.get(f)
            if j is not None:
                r.append(i)
                c.append(j)
    return sparse.csr_matrix((np.ones(len(r)), (r, c)), shape=(len(rows), len(vocab)))


def threshold_for(scores, y, precision, min_hits=5):
    """The lowest threshold whose decisions above it have at least the precision (None if none)."""
    order = np.argsort(-scores)
    tp = np.cumsum(y[order])
    n = np.arange(1, len(order) + 1)
    ok = np.where((tp / n >= precision) & (n >= min_hits))[0]
    if len(ok) == 0:
        return None
    k = ok[-1]
    return scores[order[k]]


def report(name, scores, y, t):
    hit = scores >= t
    tp, fp = int((hit & (y == 1)).sum()), int((hit & (y == 0)).sum())
    fn, tn = int((~hit & (y == 1)).sum()), int((~hit & (y == 0)).sum())
    pb = tp / (tp + fp) if tp + fp else float('nan')
    rb = tp / (tp + fn) if tp + fn else float('nan')
    po = tn / (tn + fn) if tn + fn else float('nan')
    ro = tn / (tn + fp) if tn + fp else float('nan')
    print(f'{name}: n={len(y)} bad={int(y.sum())}  bad P={pb:.3f} R={rb:.3f} ({tp}/{tp + fp} flagged)  ok P={po:.3f} R={ro:.3f}')
    return tp, fp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rows')
    ap.add_argument('out')
    ap.add_argument('-kinds', default='W')
    ap.add_argument('-precision', type=float, default=0.9)
    ap.add_argument('-min-count', type=int, default=2)
    a = ap.parse_args()
    data = read(a.rows, a.kinds.split(','))
    train = data['train']
    counts = collections.Counter(f for feats, *_ in train for f in set(feats))
    vocab = {f: i for i, f in enumerate(sorted(f for f, n in counts.items() if n >= a.min_count))}
    Xtr = matrix(train, vocab)
    ytr = np.array([y for _, y, *_ in train])
    dev = [r for r in data['dev'] if r[2]]
    test = [r for r in data['test'] if r[2]]
    Xdv, ydv = matrix(dev, vocab), np.array([r[1] for r in dev])
    Xte, yte = matrix(test, vocab), np.array([r[1] for r in test])
    print(f'train rows {len(train)} (bad {int(ytr.sum())}), features {len(vocab)}; dev {len(dev)}; test {len(test)}')
    best = None
    for C in (0.03, 0.1, 0.3, 1.0, 3.0):
        m = LogisticRegression(penalty='l1', C=C, solver='liblinear', max_iter=2000)
        m.fit(Xtr, ytr)
        s = m.decision_function(Xdv)
        t = threshold_for(s, ydv, a.precision)
        hits = 0 if t is None else int(((s >= t) & (ydv == 1)).sum())
        from sklearn.metrics import roc_auc_score
        print(f'C={C}: nonzero {int((m.coef_ != 0).sum())}, dev AUC {roc_auc_score(ydv, s):.3f}, dev bad hits at P>={a.precision}: {hits}')
        if best is None or hits > best[0]:
            best = (hits, C, m, t)
    hits, C, m, t = best
    if t is None:
        t = float('inf')
    print(f'chosen C={C}, threshold {t:.3f}')
    report('dev ', m.decision_function(Xdv), ydv, t)
    report('test', m.decision_function(Xte), yte, t)
    names = sorted(vocab, key=vocab.get)
    w = m.coef_[0]
    with open(a.out, 'w', encoding='utf-8') as o:
        o.write(f'# misread model: L1 logistic regression, C={C}, kinds {a.kinds}, dev precision {a.precision}\n')
        o.write(f'@bias\t{round(m.intercept_[0] * 1000)}\n')
        o.write(f'@threshold\t{round(t * 1000) if np.isfinite(t) else 1 << 30}\n')
        o.write(f'@keep\t{round(t * 1000) if np.isfinite(t) else 0}\n')
        for j in np.argsort([n.encode() for n in names]):
            if round(w[j] * 1000) != 0:
                o.write(names[j].replace('\\', '\\\\').replace('\t', '\\t') + f'\t{round(w[j] * 1000)}\n')


if __name__ == '__main__':
    main()
