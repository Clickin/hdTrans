"""Parse src/core/rules/*.txt into records: (file, kind, id, comments, line, tests)."""
import os, re, glob, csv, sys

def parse_dir(d):
    recs = []
    headers = {}
    for path in sorted(glob.glob(os.path.join(d, '*.txt'))):
        name = os.path.basename(path)
        lines = open(path, encoding='utf-8').read().split('\n')
        assert lines[-1] == ''
        lines = lines[:-1]
        i = 0
        hdr = []
        # header: leading comment lines up to the first rule's comment block; we take only line 1
        hdr.append(lines[0]); i = 1
        headers[name] = hdr
        pend = []
        cur = None
        for ln in lines[i:]:
            if ln.startswith('#test'):
                assert cur is not None and not pend, (name, ln)
                cur['tests'].append(ln)
            elif ln.startswith('#') or ln == '':
                pend.append(ln)
            elif ln.startswith('L ') or ln.startswith('G '):
                k, n = ln.split(' ')[:2]
                cur = dict(file=name, kind=k, id=int(n), comments=pend, line=ln, tests=[])
                recs.append(cur); pend = []
            else:
                raise SystemExit('bad line %r in %s' % (ln, name))
        assert not pend, (name, pend)
    return headers, recs

def dict_refs(dd):
    refs = {}
    for path in sorted(glob.glob(os.path.join(dd, '*.tsv'))):
        cols = None
        for ln in open(path, encoding='utf-8'):
            ln = ln.rstrip('\n')
            if ln.startswith('#'): continue
            f = ln.split('\t')
            if cols is None:
                cols = {c: i for i, c in enumerate(f)}; continue
            if 'rule' not in cols: break
            r = f[cols['rule']] if len(f) > cols['rule'] else ''
            if r and r != '0':
                refs.setdefault(int(r), []).append(dict(key=f[0], ko=f[1], pos=f[2], sem=f[3], gram=f[4], src=os.path.basename(path)))
    return refs

if __name__ == '__main__':
    h, r = parse_dir(sys.argv[1])
    from collections import Counter
    print(len(r), Counter(x['kind'] for x in r))
    print(Counter(len(x['comments']) for x in r))
