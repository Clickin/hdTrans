"""check.py [-model MODEL.tsv] STAGE_DIR...: compares the decisions, features and scores that
features.py derives from the MI/MC/MS records with the MF records the engine wrote.
Prints the first differences; exit 1 when any, 2 on bad arguments."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import features as fx


def main(argv):
    model = None
    if argv[:1] == ['-model']:
        model = fx.load_model(argv[1])
        argv = argv[2:]
    if not argv:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    compared = bad = 0
    for stage_dir in argv:
        mf = fx.records(stage_dir, 'MF')
        for line, sentences in fx.chains(stage_dir).items():
            got = []
            for chain in sentences:
                for d in fx.decisions(chain):
                    for c in d.candidates:
                        s = fx.score(model, c[3]) if model else None
                        got.append((d.item.chunk, d.item.sent, d.item.index, d.kind, c[0], s, c[3]))
            want = [(int(f[1]), int(f[2]), int(f[3]), f[4], int(f[5]), int(f[6]) if model else None,
                     [fx.unesc(x) for x in f[7:]]) for f in mf.get(line, [])]
            compared += len(want)
            if got != want:
                bad += 1
                if bad <= 5:
                    print(f'{stage_dir}: line {line} differs')
                    for g, w in zip(got + [None] * len(want), want + [None] * len(got)):
                        if g != w:
                            print('  engine  ', w)
                            print('  features', g)
                            break
    print(f'{compared} candidates compared, {bad} lines differ')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
