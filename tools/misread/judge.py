"""judge.py CHAPTERS OUT.tsv [-model ID] [-j N] [-lines N] [-sets a,b]: asks a model, per misread
decision of each line, whether the chosen Korean is right and which alternative would be.

CHAPTERS has lines "<set> <chapter> <stage dir>" (hdtrans -stages of the chapter as one
document). OUT.tsv gets one row per decision:

  chapter line decision kind ok better model

ok is 1 when the chosen reading is right, 0 when not; better is the slot of the alternative the
model names (-1 the sound reading), empty when none. Decisions already in OUT.tsv for the model
are skipped, so a run can be resumed. The endpoint is ANTHROPIC_BASE_URL (default
https://api.anthropic.com) with ANTHROPIC_API_KEY or ANTHROPIC_AUTH_TOKEN.
"""
import argparse, json, os, re, sys, threading, time, urllib.request, urllib.error
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import features as fx

INSTRUCTIONS = """Each item is one line of a Japanese web novel (JA), its machine translation into Korean (KO), and the
words of JA for which the translator chose among several Korean readings. A choice shows the word in
brackets with a little of its context, then the readings: the first one (marked *) is what the
translator chose, the others are what it could have chosen instead. Readings are dictionary-like
pieces, not finished text: verbs and adjectives are given in their dictionary form (or as a stem
followed by ~, the ending added later), endings without their verb, digits are dropped,
(을)를 means 을 or 를 depending on the word before, and ∅ means the word gives no Korean of its
own (it belongs to a longer expression or is dropped). A reading marked (sound) writes the kana as
a sound, the way a cry, a laugh or a name would be written.
For each choice decide:
  ok: true when the chosen reading is a right reading of the word in this line (a stiff but correct
      reading is right), false when it is a wrong sense, a wrong word or a misreading;
  better: when ok is false, the letter of a listed reading that would be right here, otherwise "".
Judge only the reading of that word, not the rest of the line, and do not invent readings.
Answer with one entry per choice, in order, id being the choice's number."""

SCHEMA = {"type": "object", "required": ["choices"], "additionalProperties": False,
          "properties": {"choices": {"type": "array", "items": {
              "type": "object", "required": ["id", "ok", "better"], "additionalProperties": False,
              "properties": {"id": {"type": "integer"}, "ok": {"type": "boolean"}, "better": {"type": "string"}}}}}}

LETTERS = 'abcdefghij'
MARKER = re.compile(r'[&$%][^@&$%]*@')


def show(d, c):
    """The reading of candidate c of decision d as the judge sees it."""
    target, kind, slot = c[1], d.kind, c[0]
    if kind == 'W' and slot >= 0 and MARKER.search(target):
        lemma = d.item.words[slot][5]
        if lemma and not MARKER.search(lemma):
            target = lemma
    t = MARKER.sub('~', target)
    if kind == 'E':
        t = re.sub(r'^[0-9A-Z]+', '', t)
    t = t.strip() or '∅'
    return t + ' (sound)' if slot < 0 else t


def in_context(ja, begin, end):
    if begin is None:
        return None
    return ja[max(0, begin - 8):begin] + '[' + ja[begin:end] + ']' + ja[end:end + 8]


def line_decisions(stage_dir):
    """{line: (ja, ko, [(n, decision, word in context)])} for the lines with decisions."""
    ja = {k: fx.unesc(v[0][1]) if v else '' for k, v in fx.records(stage_dir, 'N').items()}
    ko = {k: fx.unesc(v[0][1]) if v else '' for k, v in fx.records(stage_dir, 'OUT').items()}
    out = {}
    for line, sentences in fx.chains(stage_dir).items():
        text = ja.get(line, '')
        items = [it for chain in sentences for it in chain]
        where, cursor = {}, 0
        for it in items:
            k = text.find(it.surface, cursor) if it.surface.strip() else -1
            if k >= 0 and k - cursor <= 8:
                where[id(it)] = (k, k + len(it.surface))
                cursor = k + len(it.surface)
        found, n = [], 0
        for chain in sentences:
            for d in fx.decisions(chain):
                b, e = where.get(id(d.item), (None, None))
                word = in_context(text, b, e) or f'[{d.item.surface}]'
                if d.kind == 'E':
                    ds = d.item.direct_surface
                    k = -1 if b is None or not ds else text.find(ds, e, e + 4 + len(ds))
                    if k >= 0:
                        word = text[max(0, b - 8):b] + '[' + text[b:k] + '|' + ds + ']' + text[k + len(ds):k + len(ds) + 8]
                    else:
                        word = f'[{d.item.surface}|{ds}]'
                found.append((n, d, word))
                n += 1
        if found:
            out[line] = (text, ko.get(line, ''), found)
    return out


def ask(url, headers, model, prompt):
    body = json.dumps({"model": model, "max_tokens": 16000,
                       "output_config": {"effort": "low", "format": {"type": "json_schema", "schema": SCHEMA}},
                       "messages": [{"role": "user", "content": prompt}]}).encode()
    for attempt in range(6):
        try:
            req = urllib.request.Request(url, body, headers)
            with urllib.request.urlopen(req, timeout=600) as r:
                data = json.load(r)
            if data.get('stop_reason') in ('refusal', 'max_tokens'):
                raise RuntimeError('stop reason ' + data['stop_reason'])
            text = next(c['text'] for c in data['content'] if c['type'] == 'text')
            return json.loads(text)['choices']
        except (urllib.error.URLError, RuntimeError, KeyError, ValueError, StopIteration, TimeoutError) as e:
            if attempt == 5:
                raise
            time.sleep(5 << attempt)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('chapters')
    ap.add_argument('out')
    ap.add_argument('-model', default='claude-sonnet-5-5')
    ap.add_argument('-j', type=int, default=6)
    ap.add_argument('-lines', type=int, default=20)
    ap.add_argument('-sets', default='')
    a = ap.parse_args()
    base = os.environ.get('ANTHROPIC_BASE_URL', 'https://api.anthropic.com').rstrip('/')
    headers = {'content-type': 'application/json', 'anthropic-version': '2023-06-01'}
    if os.environ.get('ANTHROPIC_API_KEY'):
        headers['x-api-key'] = os.environ['ANTHROPIC_API_KEY']
    else:
        headers['authorization'] = 'Bearer ' + os.environ['ANTHROPIC_AUTH_TOKEN']
    done = set()
    if os.path.exists(a.out):
        for raw in open(a.out, encoding='utf-8'):
            f = raw.rstrip('\n').split('\t')
            if len(f) >= 7 and f[6] == a.model:
                done.add((f[0], f[1]))
    sets = set(a.sets.split(',')) if a.sets else None
    batches, batch = [], []
    for raw in open(a.chapters, encoding='utf-8'):
        if not raw.strip():
            continue
        set_, chapter, stage_dir = raw.split()
        if sets and set_ not in sets:
            continue
        for line, item in sorted(line_decisions(stage_dir).items()):
            if (chapter, str(line)) in done:
                continue
            batch.append((chapter, line, item))
            if len(batch) == a.lines:
                batches.append(batch)
                batch = []
    if batch:
        batches.append(batch)
    lock = threading.Lock()
    print(f'{len(batches)} requests', flush=True)

    def run(k, batch):
        prompt, ids, cid = [INSTRUCTIONS, ''], {}, 0
        for chapter, line, (ja, ko, found) in batch:
            prompt.append(f'JA: {ja}\nKO: {ko}')
            for n, d, word in found:
                cid += 1
                opts = '  '.join(f'{LETTERS[i]}{"*" if i == 0 else ""}) {show(d, c)}' for i, c in enumerate(d.candidates))
                prompt.append(f'  {cid}. {word}: {opts}')
                ids[cid] = (chapter, line, n, d)
            prompt.append('')
        try:
            answer = ask(f'{base}/v1/messages', headers, a.model, '\n'.join(prompt))
        except Exception as e:  # noqa: BLE001
            print(f'request {k}: {e}', flush=True)
            return
        rows = []
        for c in answer:
            if c['id'] not in ids:
                continue
            chapter, line, n, d = ids.pop(c['id'])
            better = ''
            letter = c['better'].strip().strip(')').lower()[:1]
            if not c['ok'] and letter and letter in LETTERS and 0 < LETTERS.index(letter) < len(d.candidates):
                better = str(d.candidates[LETTERS.index(letter)][0])
            rows.append(f'{chapter}\t{line}\t{n}\t{d.kind}\t{1 if c["ok"] else 0}\t{better}\t{a.model}\n')
        if ids:
            print(f'request {k}: {len(ids)} choices unanswered', flush=True)
            return
        with lock:
            with open(a.out, 'a', encoding='utf-8') as o:
                o.writelines(rows)
            print(f'request {k} done', flush=True)

    with ThreadPoolExecutor(a.j) as ex:
        for k, b in enumerate(batches):
            ex.submit(run, k, b)


if __name__ == '__main__':
    main()
