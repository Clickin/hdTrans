# 오역 고치기

번역이 틀린 줄 하나를 고쳐 `main`에 넣기까지의 과정입니다. 사람이든 에이전트든 이 순서를 따릅니다.
실행 층은 [architecture.md](architecture.md), 규칙 언어는 [rules.md](rules.md), 사전 형식은
[dictionaries.md](dictionaries.md), 코드 뜻은 [codes.md](codes.md), 판정 도구는 [testing.md](testing.md#변경-판정-e2e--모델-판정)에 있습니다.

## 순서 한눈에

1. 틀린 줄을 짧은 문장으로 재현하고 `-why`로 누가 그 단어를 골랐는지 봅니다.
2. 아래 [어디를 고칠까](#어디를-고칠까) 표에서 고칠 곳을 고릅니다. 데이터(사전 행·태그)를 먼저, 규칙을 다음, 엔진 코드는 마지막에 고칩니다.
3. 고친 것이 맞는 문장(fix)과 바뀌면 안 되는 문장(keep)을 테스트로 남깁니다.
4. 규칙을 고쳤으면 `program.hash`와 `compiled_gen.cpp`를 다시 만듭니다.
5. 전체 `ctest`를 돌립니다.
6. 고정한 기준 빌드와 웹소설 챕터를 비교하고, 바뀐 줄을 두 모델로 판정합니다. 둘 다 나빠졌다고 한 줄이 있으면 그 원인을 되돌리거나 좁힙니다.
7. 이유와 판정 숫자를 적어 커밋합니다.

## 준비

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer   # macOS
cmake --preset mac && cmake --build build/mac
H=$(mktemp -d)                                                     # 사용자 캐시를 나누지 않게
echo '彼は感情を露わにした。' | HOME=$H build/mac/hdtrans -dicts dicts
```

엔진은 사전을 사용자 캐시(`~/Library/Caches`, `~/.cache` 등)에 컴파일해 둡니다. 여러 빌드나 여러 작업을 함께
돌릴 때는 실행마다 `HOME`을 새 임시 폴더로 줍니다. 그러지 않으면 다른 빌드가 만든 캐시를 읽을 수 있습니다.

## 진단: -why

```sh
echo '競走で勝った。' | HOME=$H build/mac/hdtrans -dicts dicts -why 2>&1 | grep '^WHY' | cut -f5-17
```

고치기 전의 출력(경주로 이겼다):

```
0:0   競走  0  Single     -1    경주       ...  defaults
0:6   で    3  FirstXxx   -1    (으)로     ...  defaults
0:9   勝    0  Digit      -1    이&A2@  26 FirstXxx ... defaults  ending-defaults
```

열은 `토큰:바이트위치 표면형 선택번호 출처 id 번역 어미번호 어미출처 어미id 층 규칙 어미층 어미규칙`입니다
([architecture.md](architecture.md#누가-정했는지--why)). 볼 것:

- **층**(`local`, `global-stem`, `defaults`, `senses`, `rewrites`, `fixed` …)과 **규칙**(`particles.txt:L1276`).
  `defaults`면 어떤 규칙도 고르지 않아 사전 순서의 기본 후보가 쓰였다는 뜻입니다.
- 같은 단어의 다른 후보를 보려면 `grep -P '^競走\t' dicts/*.tsv`로 사전 행을 봅니다. 행의 `rule` 열이 그 후보를 고르는 지역 규칙 번호입니다.
- 출력이 바뀌었는데 `WHY`에 이름이 없으면 `constraints`, `rewrites`의 패스, `cg-lite`, `compose`, `cleanup`
  같은 기록하지 않는 층입니다. `-stages DIR`로 단계별 기록을 남겨 봅니다([testing.md](testing.md#한-입력-살펴보기-단계-기록--stages)).
- 기준 빌드와 비교할 때는 같은 문장을 두 빌드로 돌려 `-why` 줄을 나란히 봅니다.

## 어디를 고칠까

| 증상 | 고칠 곳 | 예 |
|---|---|---|
| 기본 역어가 늘 틀림 | `dicts/base.tsv` 행의 `ko` 열 | お仕置 `징계` → `벌` |
| 사전에 없는 말·복합어·관용구 | `dicts/words.tsv`(어휘), `words-idioms.tsv`, `words-slang.tsv`, `words-variants.tsv`에 새 행 | 疼き `욱신거림`, 引き籠もり `히키코모리` |
| 규칙이 있는데 그 단어를 못 알아봄 | 사전 행의 `sem`·`gram` 태그 | 競走·レース에 `VRw`(경기) |
| 문맥에 따라 뜻이 갈림 | `src/core/dict/builtin/senses.tsv` 행 또는 지역 규칙 | に `에게`(사람 뒤 수령 동사) |
| 규칙이 틀린 후보를 고름 | 그 규칙의 조건(`src/core/rules/<파일>.txt`) | L1276 で `에서`를 경기 문맥으로 좁힘 |
| 규칙이 없어 기본 후보가 틀림 | 새 지역 규칙 + 사전 행의 `rule` 열 | 分の `몫의` 행 + L5089 |
| 어미가 틀림 | `dicts/base.tsv`의 어미 행(품사 `S5..`) | なければと `6야 한다고` |
| 특정 문맥에서 후보를 아예 빼야 함 | `src/core/word/constraints.cpp`의 `kRules` | なる 앞 こう의 `이러한` 제거 |
| 인용·연결 조립이 틀림 | `src/core/word/`의 rewrites 패스(`grammar.cpp` 등) | たらと思う `-면 하고` |
| 의성어·감탄사 | `src/core/dict/builtin/sound.tsv`, `phrases.tsv` | |
| 문형 전체 | `src/core/dict/builtin/patterns.tsv` | 〜んだと思う |

판단 기준:

- **데이터 먼저.** 행·태그를 고쳐서 되면 규칙이나 코드를 새로 얹지 않습니다. 규칙은 이미 있는 판단을 데이터가
  못 따라갈 때, 코드는 데이터와 규칙으로 표현할 수 없을 때만 고칩니다.
- **기본 역어를 바꾸면 그 키의 모든 쓰임이 바뀝니다.** 바꾸기 전에 코퍼스에서 그 키의 다른 쓰임을 찾아 봅니다
  (`grep -h '競走' 챕터들 | head`). 문맥에 따라 갈리면 기본값을 두고 senses 행이나 규칙을 씁니다.
- **좁게 고칩니다.** 틀렸거나 애매한 출력만 겨냥합니다. 맞던 줄이 바뀌면 그 줄도 하나하나 봅니다.
- 외래어를 소리대로 옮긴 것(세이브합니까)은 틀린 것으로 보지 않습니다.
- Ehnd·꿀도르 같은 외부 사용자 사전을 `dicts/`에 합치지 않습니다. 그 형식은 따로 켜는 기능입니다
  ([ehnd-compat.md](ehnd-compat.md)).
- 사전 행에 출처 열이나 출처 주석을 달지 않습니다. 새 `words*.tsv` 행은 그 파일의 다음 `w:N` id를,
  `base.tsv`의 새 행은 겹치지 않는 id(`이름:번호`)를 씁니다. id가 비어 있으면 사전을 불러오지 못합니다.

### 사전 행 고치기

열은 파일 머리줄에 있습니다(`base.tsv`: `key ko pos sem gram dict rule id raw prio`). 자주 쓰는 품사:

| 품사 | 뜻 | `ko`의 끝 |
|---|---|---|
| `N110` | 일반 명사 | 그대로 |
| `N130` | 고유 명사 | 그대로 |
| `N311` | 부사 | 그대로 |
| `M4..` | 동사 어간(키는 어간까지: `明らかに` + する) | 활용 표지 `&AV@`(하다) 등 |
| `H7..` | い형용사 어간 | 활용 표지 `$..@` |
| `G8U0` | 형용동사 | `%A1@`(하다) |
| `S5..` | 어미 | 활용형 숫자로 시작: `6야 한다고`, `0 거지` |
| `P2..` | 조사 | `(으)로`, `에서` |

활용 표지와 어미 첫 숫자의 뜻은 [rules.md](rules.md#번역-안의-표지-d-y), 코드 전체는 [codes.md](codes.md)에 있습니다.
표지 계열은 `src/core/output/ko_families_gen.cpp`에 있지만, 같은 활용을 하는 한국어 동사의 기존 행을 찾아 그 표지를
따라 쓰는 것이 가장 안전합니다(`grep -P '\t처벌' dicts/base.tsv`). 새 행을 넣은 뒤 그 동사의 여러 활용(했다, 해서, 하는)을 꼭 돌려 봅니다.

`gram`의 `Hd`는 する가 붙는 명사(공부하다), `Tu`는 앞말에 붙여 씀(조사로 시작하는 키: `が得られていな`), `Ct`는 조수사입니다.

### 규칙 고치기

- 규칙은 고르는 단어(머리 단어)의 종류로 파일이 나뉩니다([rules.md](rules.md#주제-파일)). 지역 규칙 `L N`은
  그 번호를 `rule` 열에 적은 사전 행의 후보를 고르고, 어느 파일에 있어도 동작은 같습니다.
- 규칙 위의 한국어 주석에 무엇을 언제 고르는지 적습니다. 위 규칙을 지우면 아래 규칙이 깨어나는 일이 있으므로
  주석 없는 규칙을 두지 않습니다.
- 새 지역 규칙은 다음 번호(`grep -h '^L ' src/core/rules/*.txt | awk '{print $2}' | sort -n | tail -1`보다 1 큰 수)를
  쓰고, 사전 행의 `rule` 열에 그 번호를 적습니다. 전역 규칙은 돌아야 할 자리의 PRIO를 줍니다.
- `&&N`·`||N`의 N은 바로 그 묶음에 든 문장 수입니다. 대입(`D+0= "…"`)도 하나로 셉니다. 틀리면 `hdrulec lint`가
  `trailing tokens after the rule body`로 알립니다.
- 표기 비교는 `"L当"`(그것으로 시작), `"R戦"`(그것으로 끝남)를 씁니다. 동사 어간은 활용에 따라 표면형이 달라지므로
  `S+1== "L溢"`처럼 시작 비교를 씁니다.

## 테스트 남기기

고친 것마다 바뀌어야 하는 문장(fix)과 바뀌면 안 되는 문장(keep)을 둘 다 남깁니다. 한쪽만 있으면 A를 고치며
B를 깨뜨린 것을 알 수 없습니다.

- **규칙 바로 아래의 `#test` 줄**([rules.md](rules.md#규칙-테스트)):

  ```
  #test "君の分のお菓子" -> "너의 몫의 과자"      # 번역 전체
  #test "石から火線が飛んだ。" picks "에서"         # 이 규칙이 이 번역을 골랐음
  #test "このままでいい。" silent                  # 이 규칙이 고르지 않음
  ```

- **주제 파일별 사례 표** `tests/rules/cases/<파일>.tsv`: `rule input expected kind layer` 열, `kind`는 `fix`·`keep`.
  여러 규칙이나 사전 행이 함께 만드는 결과를 고정할 때 씁니다.
- **공용 사례 표** `tests/rules/cases.tsv`: 규칙마다 현재 출력을 고정한 750행입니다. 일부러 바꾼 출력이면 그 행의
  `expected`를 새 출력으로 고칩니다. 내가 고친 규칙의 행만 고칩니다.
- **예제 문서** `examples/*.ja.txt`·`.ko.txt`: 문서 단위 결과. 첫 줄은 두 파일에 같은 한국어 `#` 주석입니다. 출력이
  바뀌면 `.ko.txt`를 새 출력으로 고치되 첫 줄은 그대로 둡니다. 저작권이 있는 문장은 넣지 않고 직접 쓴 문장만 넣습니다.
- 엔진 코드를 고쳤으면 그 단계 옆의 단위 테스트(`tests/unit/…`)에 표 한 줄을 더합니다(예: `constraint_rules_test.cpp`).

## 다시 만들기와 확인

규칙 파일을 고쳤으면:

```sh
build/mac/hdrulec lint src/core/rules -dicts dicts 2>&1 | tail -1          # error 0
build/mac/hdrulec dump src/core/rules -hash > src/core/rules/program.hash
build/mac/hdrulec cpp src/core/rules -local 0-499 -globals -o src/core/rules/compiled_gen.cpp
cmake --build build/mac
```

그다음 전체 테스트:

```sh
H=$(mktemp -d); (cd build/mac && HOME=$H HDTRANS_DICTS="$PWD/../../dicts" ctest -j8 -E testdata)
```

`HDTRANS_DICTS`가 없으면 `examples`, `rules.cases`, `rules.tests`가 건너뛰므로 꼭 줍니다. 11개가 모두 통과해야 합니다.

## 부작용 판정

`ctest`는 고정한 문장만 봅니다. 실제 글에서 무엇이 바뀌었는지는 챕터 단위로 비교해 봅니다. 도구와 명령은
[testing.md](testing.md#변경-판정-e2e--모델-판정)에 있습니다. 요점:

- 기준은 **커밋을 고정한 별도 worktree의 빌드**입니다. 다른 사람이 계속 합치는 `main`을 기준으로 쓰지 않습니다.
- 개발 세트(고칠 때 본 챕터)와 **held-out 세트**(고칠 때 보지 않은 챕터)를 따로 돌리고 따로 보고합니다.
  개발 세트에서만 좋아지는 수정은 그 챕터에 맞춘 것일 수 있습니다.
- 바뀐 줄은 모두 Sonnet이 판정하고, Sonnet이 나빠짐·같음이라 한 줄만 GPT(high)가 다시 봅니다.
- **두 모델이 모두 나빠졌다고 한 줄이 있으면** 그 원인을 되돌리거나 조건을 좁힙니다. 하나의 수정이 여러 줄을 낫게 하고
  한 줄만 나쁘게 하면, 그 한 줄을 고칠 조건을 먼저 찾아봅니다. 남기기로 했으면 커밋 메시지에 이유를 적습니다.
- 챕터 본문과 번역, 판정 파일은 비공개 저장소(`HDTRANS_TESTDATA`)에만 둡니다. 이 저장소와 커밋 메시지에는 짧은
  직접 쓴 문장만 씁니다.

## 커밋

- 번역이 바뀌는 수정은 따로의 커밋으로, 메시지에 무엇을 왜 바꿨는지와 판정 숫자(바뀐 줄, 개선·같음·악화)를 적습니다.
- 규칙 파일을 고친 커밋에는 `program.hash`와(바뀌었으면) `compiled_gen.cpp`가 함께 들어갑니다.
- `main`에 들어간 뒤 비공개 테스트 데이터의 기대 출력을 새 출력으로 받아들입니다([testing.md](testing.md#번역이-바뀌었을-때)).

## 실제 사례

| 증상 | `-why`에서 본 것 | 원인 | 고친 것 |
|---|---|---|---|
| 競走で勝った → 경주로 이겼다 | で: `defaults (으)로` | で `에서`를 고르는 L1276을 경기 문맥으로 좁혔는데 競走 행에 경기 코드가 없었음 | `dicts/base.tsv` 競走·レース `sem`에 `VRw` |
| 君の分のお菓子 → 너의분의 과자 | 分の: `defaults 분의`, 행에 `Tu` | 수량 뒤(5회분의)를 위한 붙여 쓰기 행만 있음 | `分の 몫의` 새 행(`rule` 5089) + `L 5089 ||1 S-1== "の"` |
| 帰らなければと思う → 돌아가지 않으면이라고 | と: `particles.txt:L4664 (이)라고` | 조건 어미 뒤 인용을 다루는 행·조건이 없음 | 어미 행 `なければと 6야 한다고`(S580), L4664·L2373에 ば 조건형 분기 |
| 行けたらと思う → 갈 수 있다고 | 어미 `rewrites` | rewrites의 `QuoteTo`가 조건 たらと를 인용으로 바꿈 | `grammar.cpp`: 사고·바람 동사 앞 たらと는 `-면 하고` |
| こうなるんだ → 이러한 이다 | こう: `이러%A1@`(형용동사) + 어미 なる | 형용동사의 옛 연체형 なる로 분석 | `constraints.cpp` `kou-naru`: なる 앞 こう는 `이렇게`만 |
| お仕置きが必要 → 징계 오고가 필요 | お仕置: `징계`, き: 来 | 사전에 お仕置き가 없어 き가 따로 읽힘 | `words.tsv` お仕置き `벌`(N110 Hd), base.tsv お仕置·おしおき `벌` |
