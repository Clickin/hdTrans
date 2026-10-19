"""Rewrite src/core/rules/*.txt and tests/rules/cases/*.tsv into the files of classify.py.

  python3 generate.py ROOT [OUT.tsv] [-map MAP.tsv]

With -map, each rule goes to the file MAP.tsv names for it (columns rule, new; other rules are
classified), so rule edits made on the old layout can be laid out the same way again: check the
old files out, run this with the committed map. Running it on the new layout changes nothing.

Every record keeps its comment lines and #test lines. Inside a file the records are grouped
by head word (sorted by key), each group in rule-number order; global rules come last in PRIO
order. The old theme files are removed. OUT, if given, gets one row per rule: rule, old file,
new file, head words, reason, description.
"""
import sys, os, glob, csv
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from classify import classify, FILES

DOCS = '형식: docs/rule-language.md, 실행 순서: docs/pipeline-layers.md.'
PRINCIPLE = ('# 규칙은 그 번호를 가리키는 사전 단어(머리 단어)의 갈래로 파일을 정한다(docs/rule-language.md#주제-파일). '
             '파일 안에서는 머리 단어별로 모아 두었다.')

HEADERS = {
    'particles': [
        '# 조사 선택: 사전 품사가 조사(2**)인 단어의 번역(で→에서, 로, が→이, 을, の→의, 것, と→와, 라고…). ' + DOCS,
        PRINCIPLE,
        '# 다른 파일: 활용 어미로 분석되는 て, たら, ので 등은 endings.txt, 접속사, 감탄사 낱말은 discourse.txt.',
    ],
    'endings': [
        '# 어미 선택: 사전 품사가 어미(5**)인 항목(て, たら, ので, ません, かった, V, I…)의 번역. 연결, 종결, 시제, 부정, '
        '명사화, 인용 어미를 모두 여기 두고, 어미를 고르는 전역 규칙(G280~G360)도 둔다. ' + DOCS,
        PRINCIPLE,
        '# 다른 파일: 낱말로 분석되는 부정 형용사 ない(な)와 보조 용언 ている, てしまう는 negation-aspect.txt, '
        'され, られ, させ 꼴의 낱말은 voice-benefactive.txt, 조사는 particles.txt.',
    ],
    'negation-aspect': [
        '# 부정, 보조 용언: 낱말로 분석되는 부정 형용사 ない(な, 無, いな)와 ない로 끝나는 부정 표현(できない, に違いない, '
        'とは限らない, しかたがない…), 보조 용언 ている(してい), ておく(してお), てしまう(しま), おる(おり), '
        'ている, ておく 보조 용언 전역 규칙(G240). ' + DOCS,
        PRINCIPLE,
        '# 다른 파일: 활용 어미로 붙는 ない, ず, ません, た는 endings.txt. 동사 하나가 ません형에서 '
        '‘…할 수 없다’로 바뀌는 규칙은 그 동사가 있는 sense-verbs.txt.',
    ],
    'voice-benefactive': [
        '# 태, 수혜: 수동, 사역, 가능 꼴로 사전에 오른 낱말(され, られ, させ, させられ, 得られ…)과 수혜 보조 용언'
        '(もらう, てもらう, くれる, てくれる, させてくれる). ' + DOCS,
        PRINCIPLE,
        '# 다른 파일: 존경, 겸양의 いただく, くださる, なさる는 honorifics.txt, 어미로 붙는 꼴은 endings.txt, '
        'あげる(올리다, 들다)는 sense-verbs.txt.',
    ],
    'honorifics': [
        '# 경어, 말투: 경칭(様, さん, ちゃん, くん, 殿, 氏), 존경, 겸양 낱말(いらっしゃる, おっしゃる, いただく, くださる, ござる, '
        '伺う, 申し上げる, なさる, あそばす, お目にかかる), お, ご 존경 표현, 경칭, 겸양 구문 전역 규칙(G10, G210~G230). '
        + DOCS,
        PRINCIPLE,
        '# 다른 파일: ます, です 같은 정중 어미는 endings.txt, 参る, まいる(질리다, 가다)는 sense-verbs.txt.',
    ],
    'numbers-counters': [
        '# 수량, 조수사: 수사, 조수사, 날짜, 시각, 나이, 금액(一本, 三人, 日, 年, 分, 位, 頭, 倍…), 규칙이 주로 수량 문맥을 보는 말, '
        '남은 수량, 시간의 あと, 後, 수사 뒤 조수사 전역 규칙(G80, G110). ' + DOCS,
        PRINCIPLE,
        '# 다른 파일: 何, いくつ, いくら 같은 의문사는 pronouns-demonstratives.txt, 間, 中, 手처럼 조수사로도 쓰이지만 '
        '규칙이 주로 뜻을 가르는 명사는 sense-nouns.txt.',
    ],
    'pronouns-demonstratives': [
        '# 지시어, 의문사, 대명사: こ, そ, あ, ど 계열(これ, その, そんな, どんなに, このまま…), 의문사(何, なん, 誰, いつ, いくつ, '
        'いくら, いかが), 인칭대명사(私, 僕, 俺, 君, 自分…). ' + DOCS,
        PRINCIPLE,
        '# 다른 파일: それから, それで 같은 접속사와 そうか, どうぞ, どうも 같은 응답, 인사는 discourse.txt, '
        'そう(것 같다)는 sense-*.txt.',
    ],
    'discourse': [
        '# 담화 표지: 접속사(しかし, それから, なお, とはいえ…), 감탄사, 응답, 맞장구(ええ, うん, おい, ねえ, あら…), '
        '인사말, 상투어(どうぞ, どうも, よろしく, 今日は, ただいま, すみません). ' + DOCS,
        PRINCIPLE,
        '# 다른 파일: 문장 첫머리 조건을 쓰더라도 머리 단어가 내용어이면 그 품사의 sense-*.txt, 조사 ね, よ는 '
        'particles.txt, 웃음소리, 의성어는 sound-mimetic.txt.',
    ],
    'sound-mimetic': [
        '# 소리, 의성어, 의태어: 웃음, 한숨 같은 소리(あはは, ははは, ふう), 반복형 의성, 의태어(ぶらぶら, ごろごろ, ガラガラ), '
        'っ…り 꼴 의태 부사(ばったり, はっきり, すっかり, のんびり). ' + DOCS,
        PRINCIPLE,
        '# 다른 파일: まったく, ちょっと, ずっと 같은 보통 부사는 sense-adverbs.txt, 감탄사, 응답은 discourse.txt.',
    ],
    'sense-verbs': [
        '# 동사 뜻 선택: 머리 단어가 동사(4**)인 규칙. 문맥으로 번역을 고르는 규칙(勝る→우수하다)과 동사 자기 활용형에 '
        '따라 번역 꼴을 바꾸는 규칙(…ません→…할 수 없다), 동사 의미 호응 전역 규칙(G250~G270). ' + DOCS,
        PRINCIPLE,
        '# 다른 파일: 수동, 사역, 수혜 꼴은 voice-benefactive.txt, 존경, 겸양 동사는 honorifics.txt, ている, てしまう '
        '보조 용언은 negation-aspect.txt.',
    ],
    'sense-nouns': [
        '# 명사 뜻 선택: 머리 단어가 명사(1**)인 규칙(間→동안, 사이, 中→중, 안, 속, 手→손, 수…)과 명사, 고유명사 후보를 '
        '고르는 전역 규칙(G20~G180 중 수량 규칙 밖). ' + DOCS,
        PRINCIPLE,
        '# 다른 파일: 수사, 조수사는 numbers-counters.txt, 대명사, 의문사는 pronouns-demonstratives.txt, '
        '경칭은 honorifics.txt.',
    ],
    'sense-adjectives': [
        '# 형용사 뜻 선택: 머리 단어가 형용사(7**), 형용동사(8**)인 규칙(うまい→맛있다, 薄い→싱겁다, 얇다, 好き, 残念…). '
        + DOCS,
        PRINCIPLE,
        '# 다른 파일: 부정 형용사 ない와 ない로 끝나는 부정 표현은 negation-aspect.txt, 형용사 어미(く, くて, かった)는 '
        'endings.txt.',
    ],
    'sense-adverbs': [
        '# 부사, 연체사, 관용구 뜻 선택: 머리 단어가 부사(3**), 연체사(0**, 大の, 普通の), 관용구(9**)인 규칙. ' + DOCS,
        PRINCIPLE,
        '# 다른 파일: 접속사, 감탄사, 인사말은 discourse.txt, 지시, 의문 부사(そんなに, どんなに, なんとか)는 '
        'pronouns-demonstratives.txt, 의태어는 sound-mimetic.txt.',
    ],
    'unreferenced': [
        '# 참조 없는 규칙: 사전(dicts/*.tsv)의 rule 열이 가리키지 않아 실행되지 않는 지역 규칙과, rule 열이 비었거나 '
        '0인 행이 가리키는 빈 규칙 L 0. ' + DOCS,
        '# 사전 행이 이 번호를 가리키게 되면 그 머리 단어의 파일로 옮긴다.',
    ],
}
assert set(HEADERS) == set(FILES)


HEADER_LINES = {ln for lines in HEADERS.values() for ln in lines}


def main(root, mapfile, fixed=None):
    headers, recs, out = classify(root)
    if fixed:
        with open(fixed, encoding='utf-8') as f:
            want = {row['rule']: row['new'] for row in csv.DictReader(f, delimiter='\t')}
        out = [(r, want.get(r['kind'] + str(r['id']), new), why, heads) for r, new, why, heads in out]
    for r, _, _, _ in out:
        r['comments'] = [c for c in r['comments'] if c not in HEADER_LINES]
    rules_dir = os.path.join(root, 'src/core/rules')
    groups = defaultdict(list)
    for r, new, why, heads in out:
        key = heads.split('|')[0].split('/')[0] if heads else ''
        sortkey = (1, r['id'], '') if r['kind'] == 'G' else (0, key, r['id'])
        groups[new].append((sortkey, r))
    for path in glob.glob(os.path.join(rules_dir, '*.txt')):
        os.remove(path)
    for name in FILES:
        lines = list(HEADERS[name])
        for _, r in sorted(groups[name], key=lambda x: x[0]):
            lines += [c for c in r['comments']]
            lines.append(r['line'])
            lines += r['tests']
        with open(os.path.join(rules_dir, name + '.txt'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
    # per-theme test tables follow their rules
    where = {r['kind'] + str(r['id']): new for r, new, _, _ in out}
    cases_dir = os.path.join(root, 'tests/rules/cases')
    header, rows = None, defaultdict(list)
    for path in sorted(glob.glob(os.path.join(cases_dir, '*.tsv'))):
        if os.path.basename(path) == 'README.tsv':
            continue
        with open(path, encoding='utf-8') as f:
            lines = f.read().split('\n')
        assert lines[-1] == ''
        header = lines[0]
        for ln in lines[1:-1]:
            rows[where[ln.split('\t')[0]]].append(ln)
        os.remove(path)
    for name, lns in rows.items():
        with open(os.path.join(cases_dir, name + '.tsv'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join([header] + lns) + '\n')
    if not mapfile:
        return
    with open(mapfile, 'w', encoding='utf-8', newline='') as f:
        w = csv.writer(f, delimiter='\t', lineterminator='\n')
        w.writerow(['rule', 'old', 'new', 'heads', 'reason', 'comment'])
        for r, new, why, heads in out:
            w.writerow([r['kind'] + str(r['id']), r['file'][:-4], new, heads, why, r['comments'][0][2:]])


if __name__ == '__main__':
    args = sys.argv[1:]
    fixed = None
    if '-map' in args:
        i = args.index('-map')
        fixed = args[i + 1]
        del args[i:i + 2]
    main(args[0], args[1] if len(args) > 1 else None, fixed)
