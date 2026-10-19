"""report.py [-model MODEL.tsv] [-min SCORE] [-n N] STAGE_DIR...: lists the suspicious decisions of
stage records (hdtrans -stages, written with the misread filter off), most suspicious first.

A decision is listed when the model scores its chosen candidate at least -min (default: the
model's threshold). Each line gives the score, the stage folder and line, the Japanese word in
its context, the chosen Korean with its provenance, and the alternative the filter would take
(or "-" when no alternative scores below the model's keep). The model defaults to
tools/misread/model.tsv. Standard library only.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import features as fx


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('stages', nargs='+')
    ap.add_argument('-model', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'model.tsv'))
    ap.add_argument('-min', type=int, default=None)
    ap.add_argument('-n', type=int, default=0)
    a = ap.parse_args()
    model = fx.load_model(a.model)
    floor = model[2] if a.min is None else a.min
    found = []
    for stage_dir in a.stages:
        ja = {k: fx.unesc(v[0][1]) if v else '' for k, v in fx.records(stage_dir, 'N').items()}
        for line, sentences in fx.chains(stage_dir).items():
            for chain in sentences:
                for d in fx.decisions(chain):
                    for c in d.candidates:
                        c.append(fx.score(model, c[3]))
                    if d.candidates[0][4] < floor:
                        continue
                    text = ja.get(line, '')
                    k = text.find(d.item.surface)
                    word = d.item.surface if k < 0 else text[max(0, k - 6):k] + '[' + d.item.surface + ']' + text[k + len(d.item.surface):k + len(d.item.surface) + 6]
                    if d.kind == 'E':
                        word += ' +' + d.item.direct_surface
                    r = fx.replacement(d, model)
                    provenance = d.item.provenance if d.kind == 'W' else d.item.direct_provenance
                    found.append((d.candidates[0][4], f'{stage_dir}:{line}', word, d.candidates[0][1], provenance,
                                  '-' if r is None else r[1]))
    found.sort(key=lambda x: -x[0])
    for score, where, word, chosen, provenance, alt in found[:a.n or None]:
        print(f'{score / 1000:+.2f}\t{where}\t{word}\t{chosen}\t{provenance}\t{alt}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
