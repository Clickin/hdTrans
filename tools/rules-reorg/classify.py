"""Classify every rule of src/core/rules/*.txt into the new theme files.

Principle: a local rule decides the translation of the dictionary word(s) whose `rule` column
points at it (the head word). The head word's class picks the file, so every rule of one word
lands in one file. A head is the pair (key, first digit of the POS code): な the negative
adjective, な the verb なる, な the particle and な the ending are four heads.

  python3 classify.py ROOT OUT.tsv      ROOT = worktree; writes rule -> old -> new -> reason
"""
import sys, os, re, csv
from collections import Counter, defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse import parse_dir, dict_refs

FILES = {
    'particles': '조사',
    'endings': '어미',
    'negation-aspect': '부정·보조 용언',
    'voice-benefactive': '태·수혜',
    'honorifics': '경어·말투',
    'numbers-counters': '수량·조수사',
    'pronouns-demonstratives': '지시어·의문사·대명사',
    'discourse': '담화 표지',
    'sound-mimetic': '소리·의성어·의태어',
    'sense-verbs': '동사 뜻',
    'sense-nouns': '명사 뜻',
    'sense-adjectives': '형용사 뜻',
    'sense-adverbs': '부사·연체사·관용구 뜻',
    'unreferenced': '참조 없음',
}

# Global rules (G PRIO) have no head word; they are placed by what they select.
GLOBAL = {
    10: ('honorifics', '사람 이름 뒤 様·氏·君·さん 경칭'),
    20: ('sense-nouns', '사람 이름 뒤 직업·신분 명사'),
    30: ('sense-nouns', '명사 연속에서 명사 후보'),
    40: ('sense-nouns', 'Z6 명사 + 명사'),
    50: ('sense-nouns', 'Wa+Wb 명사 짝'),
    60: ('sense-nouns', 'Wa+Wb 명사 짝'),
    70: ('sense-nouns', 'Mo 쉼표 나열'),
    80: ('numbers-counters', '수사 뒤 조수사'),
    90: ('sense-nouns', '명사 의미 호응'),
    100: ('sense-nouns', 'Td·On 한 단어 문장의 고유명사'),
    110: ('numbers-counters', '수사 뒤 Z7 명사'),
    120: ('sense-nouns', '쉼표로 나열된 고유명사'),
    130: ('sense-nouns', '・로 이은 고유명사'),
    140: ('sense-nouns', 'UDr + QCT 지명'),
    150: ('sense-nouns', '지명 + 都·市·県·区·駅'),
    160: ('sense-nouns', '고유명사 연속'),
    170: ('sense-nouns', '고유명사 연속'),
    180: ('sense-nouns', 'Mk 뒤 고유명사'),
    190: ('particles', '명사 뒤 조사 후보 해 주세요'),
    200: ('particles', '쉼표 뒤 Z0 조사'),
    210: ('honorifics', 'お+동사+する 겸양'),
    220: ('honorifics', 'お+동사+いたす 겸양, である'),
    230: ('honorifics', '동사+上げる 겸양(申し上げる)'),
    240: ('negation-aspect', 'ている·ておく의 い·お 보조 용언'),
    250: ('sense-verbs', '동사 의미 호응'),
    260: ('sense-verbs', '동사 의미 호응'),
    270: ('sense-verbs', '동사 의미 호응'),
    280: ('endings', '어미 -어 + Ds 동사'),
    290: ('endings', '어미 Dx 연결'),
    300: ('endings', '어미 Dx -어'),
    310: ('endings', '어미 -고'),
    320: ('endings', '어미 -어서'),
    330: ('endings', '어미 -어 기본'),
    340: ('endings', '형용사 くて 연결 -고/-어서'),
    350: ('endings', '형용동사 で 연결 -고/-어서'),
    360: ('endings', '형용동사 に 어미 에'),
}

# --- word lists -----------------------------------------------------------------------------

DEMO_STEMS = ['こ', 'そ', 'あ', 'ど']
DEMO_SUFFIX = ['れ', 'れら', 'こ', 'の', 'んな', 'んなに', 'う', 'ういう', 'ういった', 'うやって', 'うして',
               'ちら', 'っち', 'いつ', 'やつ', 'なた']
DEMO = {s + x for s in DEMO_STEMS for x in DEMO_SUFFIX} | {'あそこ', 'どれほど', 'どれだけ', 'どれくらい',
        'どれぐらい', 'こういう', 'そういう', 'ああいう', 'どういう', 'こうい', 'そうい', 'どうい', 'ああい'}
DEMO -= {'あう', 'こう', 'どなた'}  # あう 会う; こう 高·校 kana; どなた is in WH
DEMO |= {'こう'}
INTERROG = {'何', 'なに', 'なん', 'ナニ', 'ナン', '誰', 'だれ', 'どなた', 'いつ', 'いくら', 'いくつ', 'なぜ',
            'どうして', '如何', 'いかが', '何故', '幾つ', '幾ら', 'どちら様'}
PRONOUN = {'私', 'わたし', 'わたくし', 'あたし', '僕', 'ぼく', 'ボク', '俺', 'おれ', 'オレ', '君', 'きみ',
           'あなた', 'あんた', 'お前', 'おまえ', '彼', 'かれ', '彼女', '自分', '我々', 'われわれ', '我',
           'われ', 'わし', 'こいつ', 'そいつ', 'あいつ', 'どいつ', 'みんな', '皆', '皆さん', 'みなさん',
           '奴', 'やつ', '貴方', '貴様', 'きさま', '彼ら', '私たち', '僕ら', '僕たち', '俺たち', 'お宅',
           'おたく', '当方', '小生', '拙者', 'わが', '我が'}
# Korean deictic and interrogative stems: a kana key counts as a demonstrative only if its
# translations start with one of these (こうか 効果, そうさ 操作 do not).
KO_DEIXIS = ('이', '그', '저', '어느', '어떻', '어떤', '어디', '여기', '거기', '저기', '무엇', '뭐', '무슨',
             '누구', '누가', '언제', '몇', '얼마', '왜', '이렇', '그렇', '저렇', '어째', '어쩐', '아무',
             '이런', '그런', '저런', '이쪽', '그쪽', '저쪽', '어느 쪽', '어떻게', '도대체', '어때', '어떠', '어땠')

DISCOURSE = {
    # conjunctions
    'しかし', 'しかして', 'だが', 'だけど', 'けど', 'けれど', 'けれども', 'でも', 'それでも', 'そして', 'それで',
    'それから', 'それに', 'それとも', 'そこで', 'すると', 'だから', 'ですから', 'したがって', '従って',
    'つまり', 'すなわち', '即ち', 'なお', '尚', 'また', '又', 'さらに', '更に', 'ただし', '但し', 'ただ',
    'もっとも', 'ところで', 'ところが', 'さて', 'では', 'じゃあ', 'じゃ', 'では', 'それでは', 'それじゃ',
    'ゆえに', '故に', 'なぜなら', 'とはいえ', 'とはいうものの', 'かつ', '且つ', 'および', '及び', 'ならびに',
    'あるいは', '或いは', 'または', '又は', 'もしくは', 'ついで', '次いで', 'そのうえ', 'その上', 'しかも',
    'おまけに', 'ちなみに', '要するに', 'いずれにせよ', 'とにかく', 'ともかく', 'さもないと', 'そうして',
    'そうすると', 'そうしたら', 'したら', 'そしたら', 'すなわち', 'にもかかわらず', 'それなのに', 'なのに',
    'あと',
    # responses, interjections, fillers
    'はい', 'いいえ', 'いえ', 'いや', 'ええ', 'うん', 'ううん', 'ああ', 'あら', 'あれ', 'おや', 'まあ', 'さあ',
    'ほら', 'ねえ', 'ねぇ', 'なあ', 'おい', 'こら', 'もしもし', 'あのう', 'あの', 'えっと', 'ええと', 'えーと',
    'うーん', 'おう', 'おうおう', 'へえ', 'ほう', 'よし', 'やれやれ', 'おっと', 'そうか', 'そうそう', 'なるほど',
    'ホント', 'ほんと', '本当', 'まさか', 'やった', 'え', 'ん', 'すごいね', 'すごいな', 'だね',
    # greetings and set phrases
    'こんにちは', '今日は', 'こんばんは', '今晩は', 'おはよう', 'ただいま', 'よろしく', 'どうぞ', 'どうも',
    'ありがとう', 'すみません', 'すいません', 'ごめん', 'ごめんなさい', 'さようなら', 'お大事に',
    'どういたしまして', 'お楽しみに', 'これはどうも', 'いらっしゃいませ', 'おめでとう', 'おかげさまで',
    'お疲れ様', 'ご苦労さま', 'ご苦労様', '御苦労さま', '御苦労様',
}
# Of DISCOURSE, あと goes with numbers (あと 3人 / あと、) since most of its rules are quantity ones.
NUMBER_WORDS = {'あと', '後', '半', '半分', '第', '毎', '余り', '余りの', 'あまりの', '以上', '以下', '未満',
                '前後', 'ぶり', '振り', 'おき', '目', '位', '度', '倍', '割', '年', '月', '日', '時', '分', '秒',
                '歳', '才', '人', '個', '回', '番', '号', '階', '円', '点', '件', '頭', '羽', '尾', '席', '機',
                '品', '台', '本', '枚', '匹', 'つ', '年間', '週間', '時間', 'ヶ月', 'ヵ月', 'か月', '年代',
                '歳代', '約', '計', '満', '頃', 'ころ', 'ごろ', '近く', 'あたり', '当り', '当たり', '強', '弱',
                '余', '以内', '数', '幾', '毎日', '毎年', '一人', 'ひとり', '二人', 'ふたり', '一つ', 'ひとつ',
                '一緒', '何時', '何人', '何度', '何回', '何年', '何日', '何時頃', '大台', '割に', '割り',
                '分の', '年生', '年ほど前', '日前', '分前', '日付', '日付け', '年末', '年初め', '年平均',
                '万', '千', '百', '十', '億', '兆', '一', '二', '三', '万人', '万世帯', '一日', '一年',
                '二日', '二本', '八本', 'ぷん', 'ふん', '一時', '一対', '一足', '日中', '日朝', '時半',
                '時時', '日連続', '年ごと', '年ごろ', 'ヶ月間', 'ヶ月毎', '日過ぎから', '日前後', '人前',
                'つ子', '字詰め', '段階', '額', '相当', 'かれこれ', '限り', 'きり', '大分', '過半数をとれ',
                '歳ぐらいで', '歳で', 'ぐらいの人', '十分', '十分に', 'おいくつ', '1つ'}
NUMBER_RE = re.compile(r'^[0-9０-９一二三四五六七八九十百千万億兆両数幾何]+'
                       r'(人|回|度|年|日|時|個|本|枚|歳|才|つ|分|番|階|件|倍|足|対|晩|週|ヶ|か月|ヵ月|円|万|通り|目|杯)'
                       r'|^[0-9０-９]')
NUMBER_TXT = re.compile(r'(숫자|수사|조수사|수량|수치|개수|나이|시각|날짜|금액|횟수|분량|인원|몇|단위|계수)')
NUMBER_KO = re.compile(r'^(몇|한 |두 |세 |네 |하나|둘|셋|넷|첫|[0-9])')

HONORIFIC_EXACT = {'様', 'さま', 'さん', 'ちゃん', 'くん', '殿', 'どの', '氏', '先生', 'せんせい', '氏系', '氏との',
                   '皆さん', 'みなさん', '御'}
HONORIFIC_RE = re.compile(r'^(いらっしゃ|おっしゃ|仰っしゃ|仰しゃ|仰|召し上が|めしあが|申し上げ|申しあげ|'
                          r'申し上|申せ|申し訳|申$|伺|うかが|お伺|御伺|いただ|頂|ござ|御座|なさ|くださ|下さ|'
                          r'あそば|遊ばさ|いたし|致し|存じ|ぞんじ|拝見|拝借|お目にかか|お越し|'
                          r'ご覧|御覧|ご利用|お招き|お納め|ご登録|ご一緒|お済み|お困り|お住まい|お勤め|'
                          r'お詫び|お礼|御礼|おれい|自愛|幸甚|でござ|がござ|ではござ|しなさ|何になさ|御目に|'
                          r'させていただ|していただ|購入いただ|ご利用いただ|お教え)'
                          r'|(様|さま|さん|ちゃん|くん|殿)$|^(お|ご|御).+(いた|いたし|致し|申し上げ|申しあげ)$')
HONORIFIC_NOT = re.compile(r'^(同じ様|同様|様に|様子|申立|申し立|頂上|頂点|仰天|仰向|仰ぎ|仰ぐ|拝借|たくさん|沢山|私同)|なさそう|ござる$')
HONORIFIC_KO = re.compile(r'(님|께|드리|여쭈|여쭙|말씀|계시|주무|잡수|진지|뵙|뵈|올리|시다|십시오|세요|셔|셨|'
                          r'하시|되시|가시|오시|보시|주시|드시|습니다|니다|ㅂ니다)')

# Voice: passive, causative, potential (as words), benefactive auxiliaries.
VOICE_RE = re.compile(r'(られ|され|させ|させられ|せられ|かせ|わせ|たせ|ませ$|らせ|がせ|ばせ|なせ|'
                      r'[かがさたなばまわら]れ)(な|てい|ていな|てはい|得|え|ず)?$')
VOICE_NOT = {'なれ', 'あれ', 'まれ', 'われ', 'かれ', 'これ', 'それ', 'だれ', 'どれ', 'たれ', 'はなれ', '離れ',
             'ませ', 'わかれ', '分かれ', '別れ', 'わすれ', '忘れ', 'くずれ', '崩れ', 'つかれ', '疲れ', 'おくれ',
             '遅れ', '入れ', 'いれ', 'くれ', 'かくれ', '隠れ', 'うまれ', '生まれ', '生れ', 'おぼれ', 'あばれ',
             'ながれ', '流れ', 'あふれ', 'こわれ', '壊れ', 'たおれ', '倒れ', 'ふれ', '触れ', 'まぎれ',
             'みだれ', '乱れ', 'はれ', '晴れ', 'ぬれ', '濡れ', 'つぶれ', '潰れ', 'やぶれ', '敗れ', '破れ',
             'あきれ', 'おそれ', '恐れ', 'まみれ', 'ゆれ', '揺れ', 'それ', 'あらわれ', '現れ', 'はずれ',
             '外れ', 'もれ', '漏れ', 'ずれ', 'さばかれ', 'かわれ', 'すぐれ', '優れ', '割れ', 'われ', '切れ',
             'きれ', '慣れ', 'なれ', '馴れ', '枯れ', 'かれ', '取れ', 'とれ', '知れ', 'しれ', '連れ', 'つれ',
             '暮れ', 'くれ', 'ばれ', '荒れ', 'あれ', '腫れ', '惚れ', 'ほれ', '振れ', '汚れ', 'よごれ', '剥がれ',
             'はがれ', '逃れ', 'のがれ', '遅れ', '溢れ', '紛れ', '憧れ', 'あこがれ', '焦がれ', '捩れ', '外され',
             '見せ', 'みせ', '乗せ', 'のせ', '載せ', '寄せ', 'よせ', '伏せ', 'ふせ', '合わせ', 'あわせ', '任せ',
             'まかせ', '浴びせ', 'あびせ', '着せ', 'きせ', '果たせ', '済ませ', 'すませ', '幸せ', 'しあわせ',
             '知らせ', 'しらせ', '急がせ', '似せ', '痩せ', 'やせ', '馳せ', 'かぶせ', '被せ', '聞かせ', '聞こえ',
             '生まれ', 'うまれ', '合わせ', '分かれ', '欠かせ', 'あそばせ', 'あそばされ', 'なられ', 'おかれ'}
BENEFACTIVE_RE = re.compile(r'^(もら|貰|くれ|呉れ|てあげ|でもら|てもら|てくれ|でくれ|てや|させてくれ)|'
                            r'(てもら|でもら|てくれ|でくれ|てあげ|てやる|てや)|もらえ|もらいた|もらっ')

NEGATION_KO = re.compile(r'(않|없|아니|아$A3@|못|안 |안되|\$D7@|\$DH@|\$FA@|\$A3@|\$AB@|\$Kq@|말다|마라)')
ASPECT_RE = re.compile(r'^(し|っ|ん|い)?(てい|でい|てお|でお|てしま|でしま)$|^(しま|ちゃ|ちま|おり|おりま)$')
NOT_NEG = ('はかな', '少な', 'すくな', '危な', 'あぶな', 'きたな', '汚な', 'おさな', '幼な', 'つたな', '拙な',
           'せつな', '切な', 'ぎこちな', 'いさぎよ', 'あどけな', 'しがな', 'おとな', '大人', 'やな')

MIMETIC_RE = re.compile(r'^([ぁ-ゖァ-ヺー]{2})\1(と|に|っと|って)?$|'
                        r'^[ぁ-ゖァ-ヺ][っッ][ぁ-ゖァ-ヺ][りリ](と|に)?$|'
                        r'^[ぁ-ゖァ-ヺ][ぁ-ゖァ-ヺ][りリ]と$|'
                        r'^[ぁ-ゖァ-ヺ][んン][ぁ-ゖァ-ヺ][りリ](と|に)?$')
MIMETIC_NOT = {'まったく', 'すっかり', 'ちょっと', 'きっと', 'ずっと', 'もっと', 'やっと', 'そっと', 'さっき',
               'しっかり', 'はっきり', 'うっかり', 'がっかり', 'すっきり', 'ゆっくり', 'さっぱり', 'そっくり',
               'ぴったり', 'ピッタリ', 'ばったり', 'ぽっかり', 'たっぷり'}
# Of MIMETIC_NOT the Xっ Yり adverbs are still mimetics (擬態語); keep them out only of the
# generic sound check below and add them back explicitly.
MIMETIC_ADVERBS = {'すっかり', 'しっかり', 'はっきり', 'うっかり', 'がっかり', 'すっきり', 'すっきりと',
                   'ゆっくり', 'さっぱり', 'そっくり', 'ぴったり', 'ピッタリ', 'ピッタシ', 'ばったり',
                   'ぽっかり', 'たっぷり', 'どんより', 'ずらりと', 'ぐっすり', 'こっそり', 'ひっそり',
                   'じっくり', 'にっこり', 'がっちり', 'きっちり', 'ばっちり', 'のんびり', 'ぼんやり',
                   'ひんやり', 'しっとり', 'しっとりと', 'すっと', 'ぱっと', 'さっと', 'はっと', 'ほっと',
                   'ぎりぎり', 'ぎっしり', 'びっしり', 'ぐったり', 'がらりと', 'ころりと', 'ずらり'}
REDUP_NOT = ('そこそこ', 'たびたび', 'なかなか', 'いよいよ', 'まだまだ', 'もともと', 'ますます', 'しばしば',
             'いろいろ', 'わざわざ', 'ときどき', 'それぞれ', 'ひとびと', 'まあまあ', 'そもそも', 'あんまり',
             'ほんのり', 'みすみす', 'くれぐれ', 'これこれ', 'しみじみ', 'ちかぢか', 'つくづく', 'たまたま',
             'またまた', 'まずまず', 'いやいや', 'どうどう', 'ゆくゆく', 'はやばや', 'そろそろ')
SOUND_KO = re.compile(r'^(하하|후후|흐흐|히히|헤헤|으으|아하하|우와|쾅|탕|퍽|쿵)')


def load_sound(root):
    snd = {}
    for p in ['src/core/dict/builtin/sound.tsv', 'src/core/dict/builtin/extra/sound.tsv']:
        for ln in open(os.path.join(root, p), encoding='utf-8'):
            if ln.startswith('//'):
                continue
            f = ln.rstrip('\n').split('\t')
            if len(f) >= 3:
                snd[f[0]] = f[2]
    return snd


def posd(pos):
    return pos[1:2] if len(pos) > 1 else '-'


SENSE_BY_POS = {'4': 'sense-verbs', '1': 'sense-nouns', '7': 'sense-adjectives', '8': 'sense-adjectives',
                '3': 'sense-adverbs', '0': 'sense-adverbs', '9': 'sense-adverbs', '6': 'sense-adverbs'}


def head_class(key, d, rows, comments, snd):
    """Class of the head (key, POS digit) from its dictionary rows and its rules' comments."""
    kos = [r['ko'] for r in rows]
    grams = ''.join(r['gram'] for r in rows)
    text = ' '.join(comments)
    if d == '2':
        return 'particles', '조사 품사(2**)'
    if d == '5':
        return 'endings', '어미 품사(5**)'
    # あと·後 (あと 3人, あと、…) stay together with the quantity words
    if key in ('あと', '後') and d == '1':
        return 'numbers-counters', 'あと·後: 남은 수량·시간 관계'
    if key in DISCOURSE and d in '13069':
        return 'discourse', '접속사·감탄사·응답·인사말'
    if snd.get(key) == 'lead' and d in '1309' and len(key) > 1 and key not in ('ちょっと', 'そう', 'さらに'):
        return 'discourse', 'sound.tsv lead(말머리)'
    if snd.get(key) in ('intj', 'ono') and d not in '45':
        return 'sound-mimetic', 'sound.tsv ' + snd[key]
    if key in MIMETIC_ADVERBS or (MIMETIC_RE.match(key) and d in '1340' and key not in MIMETIC_NOT
                                  and not key.startswith(REDUP_NOT)):
        return 'sound-mimetic', '의태어 꼴(반복형·っ…り)'
    if re.search(r'(의성어|의태어|웃음소리를 흉내)', text) and d in '13':
        return 'sound-mimetic', '설명: 의성어·의태어'
    # deixis
    if key in INTERROG or key in PRONOUN:
        return 'pronouns-demonstratives', '의문사·인칭대명사'
    deictic_ko = sum(1 for k in kos if k.startswith(KO_DEIXIS)) * 2 >= len(kos)
    if key in DEMO and key != 'そう' and d in '13049' and (deictic_ko or not re.fullmatch(r'[ぁ-ゖ]+', key)):
        return 'pronouns-demonstratives', '지시어(こ·そ·あ·ど)'
    if key in ('どうか', 'どうか、', 'うち', 'そう') or key.startswith('そうだ'):
        pass
    elif (any(key.startswith(p) for p in DEMO if len(p) >= 2) or key.startswith(('何', 'なに', 'なん', '誰')))\
            and d in '1309' and deictic_ko and not key.startswith(('どうぞ', 'どうも', 'そうか', 'そうそう',
                                                                    'それから', 'それで', 'それに', 'そして')):
        return 'pronouns-demonstratives', '지시어·의문사로 시작하는 말'
    # honorifics
    if key in HONORIFIC_EXACT and d in '1':
        return 'honorifics', '경칭·높임 접사'
    if HONORIFIC_RE.search(key) and not HONORIFIC_NOT.search(key) and \
            (d == '1' or not re.search(r'(様|さま|さん|ちゃん|くん|殿)$', key)):
        return 'honorifics', '존경·겸양 낱말'
    if key.startswith(('お', 'ご', '御')) and len(key) > 2 and sum(1 for k in kos if HONORIFIC_KO.search(k)) * 2 > len(kos) \
            and d in '14709':
        return 'honorifics', 'お·ご 존경 표현'
    # numbers: a numeral key, or a word whose rules are mostly about numbers
    if NUMBER_RE.search(key) and d in '130':
        return 'numbers-counters', '수사+조수사 꼴'
    numeric = sum(1 for c in comments if NUMBER_TXT.search(c))
    if d in '130' and comments and numeric * 2 >= len(comments) and (
            re.search(r'(Ct|Nj|Nd)', grams) or key in NUMBER_WORDS):
        return 'numbers-counters', '수량어(Ct·Nj·Nd 또는 단위어)이고 규칙 설명이 수량 문맥'
    if re.search(r'(Nj|Nd)', grams) and d in '130':
        return 'numbers-counters', '수사 문법 코드(Nj·Nd)'
    # voice and benefactive
    if d in '47' and BENEFACTIVE_RE.search(key) and not key.startswith(('くれぐれ', 'くれる')):
        return 'voice-benefactive', '수혜 보조 용언(もらう·くれる·あげる)'
    if d in '47' and VOICE_RE.search(key) and not any(key.endswith(v) or key.endswith(v + 'な') for v in VOICE_NOT) \
            and len(key) >= 2:
        return 'voice-benefactive', '수동·사역 꼴(れる·られる·せる·させる)'
    if d in '47' and re.search(r'(得|うる|える)$', key) and re.search(r'(得る|가능|수 있)', text):
        return 'voice-benefactive', '가능 꼴(得る)'
    # negation adjectives and aspect auxiliaries
    if d == '7' and re.search(r'な$|ない$|なく$|無$', key) and not key.endswith(NOT_NEG) and sum(1 for k in kos if NEGATION_KO.search(k)) * 2 > len(kos):
        return 'negation-aspect', '부정 형용사(ない)'
    if d in '47' and ASPECT_RE.search(key) and len(key) <= 6:
        return 'negation-aspect', '보조 용언(ている·てしまう·ておく)'
    if d in SENSE_BY_POS:
        return SENSE_BY_POS[d], '뜻 선택: 품사 ' + d
    return 'sense-adverbs', '뜻 선택: 품사 미상'


def classify(root):
    headers, recs = parse_dir(os.path.join(root, 'src/core/rules'))
    refs = dict_refs(os.path.join(root, 'dicts'))
    snd = load_sound(root)
    # rows and comments per head
    head_rows = defaultdict(list)
    head_comments = defaultdict(list)
    rec_by_id = {r['id']: r for r in recs if r['kind'] == 'L'}
    for rid, rows in refs.items():
        for x in rows:
            h = (x['key'], posd(x['pos']))
            head_rows[h].append(x)
            if rid in rec_by_id:
                head_comments[h].append(rec_by_id[rid]['comments'][0])
    hclass = {h: head_class(h[0], h[1], rows, head_comments[h], snd) for h, rows in head_rows.items()}
    out = []
    for r in recs:
        if r['kind'] == 'G':
            new, why = GLOBAL[r['id']]
            out.append((r, new, 'G: ' + why, ''))
            continue
        rows = refs.get(r['id'], [])
        if not rows:
            out.append((r, 'unreferenced', '사전 행이 가리키지 않음', ''))
            continue
        votes = Counter()
        for x in rows:
            votes[hclass[(x['key'], posd(x['pos']))]] += 1
        (new, why), _ = votes.most_common(1)[0]
        heads = '|'.join(sorted({x['key'] + '/' + x['pos'] for x in rows}))
        out.append((r, new, why, heads))
    return headers, recs, out


if __name__ == '__main__':
    root, dst = sys.argv[1], sys.argv[2]
    headers, recs, out = classify(root)
    with open(dst, 'w', encoding='utf-8', newline='') as f:
        w = csv.writer(f, delimiter='\t', lineterminator='\n')
        w.writerow(['rule', 'old', 'new', 'heads', 'reason', 'comment'])
        for r, new, why, heads in out:
            w.writerow([r['kind'] + str(r['id']), r['file'][:-4], new, heads, why, r['comments'][0][2:]])
    c = Counter(new for _, new, _, _ in out)
    for k in FILES:
        print(f'{c[k]:5d} {k}')
    print(sum(c.values()), 'moved', sum(1 for r, new, _, _ in out if r['file'][:-4] != new))
