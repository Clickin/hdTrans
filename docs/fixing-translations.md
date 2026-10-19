# 오역 고치기

번역이 틀린 줄을 고쳐 `main`에 넣는 순서입니다. 실행 층은 [pipeline-layers.md](pipeline-layers.md), 규칙 언어는
[rule-language.md](rule-language.md), 사전 형식은 [dictionary-files.md](dictionary-files.md), 코드 뜻은
[dictionary-codes.md](dictionary-codes.md), 판정 도구는 [testing.md](testing.md#변경-판정-e2e--모델-판정)에 있습니다.

1. 짧은 문장으로 재현하고 `-why`로 누가 그 단어를 골랐는지 봅니다.
2. 고칠 곳을 고릅니다. 사전 행과 태그를 먼저, 규칙을 다음, 엔진 코드는 마지막에 봅니다.
3. 고친 문장(fix)과 바뀌면 안 되는 문장(keep)을 테스트로 남깁니다.
4. 규칙을 고쳤으면 `program.hash`와 `compiled_gen.cpp`를 다시 만듭니다.
5. `ctest`를 돌립니다.
6. 고정한 기준 빌드와 챕터 단위로 비교하고 바뀐 줄을 판정합니다.
7. 이유와 판정 숫자를 적어 커밋합니다.

## 재현과 -why

```sh
cmake --preset mac && cmake --build build/mac   # macOS 빌드가 실패하면 docs/build.md의 DEVELOPER_DIR
H=$(mktemp -d)
echo '競走で勝った。' | HOME=$H build/mac/hdtrans -dicts dicts -why 2>&1 | grep '^WHY' | cut -f5-17
```

사전 캐시는 사용자마다 하나라서, 빌드 여러 개를 함께 돌릴 때는 실행마다 `HOME`을 새 임시 폴더로 줍니다.

`-why` 열은 [pipeline-layers.md](pipeline-layers.md#누가-정했는지--why)에 있습니다. 끝의 층과 규칙(`local particles.txt:L1276`)을
봅니다. `defaults`는 어떤 규칙도 고르지 않았다는 뜻입니다. 같은 단어의 다른 후보는 `grep -P '^競走\t' dicts/*.tsv`로 보고,
행의 `rule` 열이 그 후보를 고르는 지역 규칙입니다. 출력이 바뀌었는데 `WHY`에 이름이 없으면 `-stages DIR`로 단계 기록을 봅니다.

## 어디를 고칠까

| 증상 | 고칠 곳 | 예 |
|---|---|---|
| 기본 역어가 늘 틀림 | `dicts/base.tsv` 행의 `ko` | お仕置 `징계` → `벌` |
| 사전에 없는 말 | `dicts/words*.tsv`에 새 행 | 疼き `욱신거림` |
| 규칙이 그 단어를 못 알아봄 | 행의 `sem`, `gram` | 競走에 `VRw` |
| 문맥에 따라 뜻이 갈림 | `src/core/dict/builtin/senses.tsv`나 지역 규칙 | |
| 규칙이 틀린 후보를 고름 | 규칙 조건 | L1276을 경기 문맥으로 좁힘 |
| 고를 규칙이 없음 | 새 행 + 새 지역 규칙 | 分の `몫의` + L5089 |
| 어미가 틀림 | `base.tsv`의 어미 행(`S5..`) | なければと `6야 한다고` |
| 그 문맥에서 후보를 빼야 함 | `src/core/word/constraints.cpp`의 `kRules` | なる 앞 こう |
| 인용, 연결 조립이 틀림 | `src/core/word/`의 rewrites 패스 | たらと思う `-면 하고` |
| 의성어, 감탄사, 문형 | `builtin/sound.tsv`, `phrases.tsv`, `patterns.tsv` | |

- 기본 역어를 바꾸면 그 키의 모든 쓰임이 바뀝니다. 코퍼스에서 다른 쓰임을 먼저 찾아 봅니다.
- 틀렸거나 애매한 출력만 고칩니다. 외래어 음역은 틀린 것으로 보지 않습니다.
- Ehnd 같은 외부 사용자 사전은 `dicts/`에 합치지 않습니다([ehnd-compat.md](ehnd-compat.md)).
- 새 행에는 id가 있어야 사전을 불러옵니다. `words*.tsv`는 다음 `w:N`, `base.tsv`는 겹치지 않는 `이름:번호`를 씁니다.

### 사전 행

열은 파일 머리줄에 있습니다. 활용하는 말은 같은 활용을 하는 기존 행을 찾아 활용 표지를 따라 씁니다
(`grep -P '\t처벌' dicts/base.tsv`). 새 행을 넣으면 여러 활용(했다, 해서, 하는)을 돌려 봅니다.

| 품사 | 뜻 | `ko` |
|---|---|---|
| `N110`, `N130` | 명사, 고유 명사 | 그대로 |
| `N311` | 부사 | 그대로 |
| `M4..` | 동사 어간 | 활용 표지로 끝남(`&AV@` 하다) |
| `H7..` | い형용사 어간 | `$..@` |
| `G8U0` | 형용동사 | `%A1@` |
| `S5..` | 어미 | 활용형 숫자로 시작(`6야 한다고`) |
| `P2..` | 조사 | `(으)로` |

`gram`의 `Hd`는 する가 붙는 명사, `Tu`는 앞말에 붙여 씀, `Ct`는 조수사입니다.

품사는 띄어쓰기에도 영향을 줍니다. 명사(`N1..`) 뒤의 짧은 명사는 붙여 쓰는 경우가 있어(몫의자리), 꾸미는 말이면
연체사(`E0H0`)로 둡니다. 같은 키의 후보끼리 품사가 다르면 분석이 다른 후보를 고를 수 있으니 함께 맞춥니다.

### 규칙

- 파일은 규칙이 고르는 단어의 종류로 나뉩니다([rule-language.md](rule-language.md#주제-파일)). 지역 규칙은 어느 파일에 있어도 동작이 같습니다.
- 규칙 위에 무엇을 언제 고르는지 한 줄로 적습니다. 위 규칙을 지우면 아래 규칙이 깨어날 수 있습니다.
- 새 지역 규칙은 지금 가장 큰 번호 다음을 쓰고 사전 행의 `rule` 열에 적습니다.
- `&&N`, `||N`의 N은 그 묶음의 문장 수이고 대입도 하나로 셉니다.
- 동사 어간은 `"L溢"`(그것으로 시작)로 비교합니다. `"R戦"`는 그것으로 끝남입니다.
- `hdrulec` 사용법과 진단은 [rule-compiler.md](rule-compiler.md#사용법)에 있습니다.

## 테스트

- 규칙 아래 `#test` 줄: `-> "번역"`, `picks "번역"`, `silent`([rule-language.md](rule-language.md#규칙-테스트)).
- `tests/rules/cases/<파일>.tsv`: `rule input expected kind layer`, kind는 `fix`, `keep`.
- `tests/rules/cases.tsv`: 규칙마다 현재 출력을 고정한 표. 일부러 바꾼 내 규칙의 행만 고칩니다.
- `examples/`: 출력이 바뀌면 `.ko.txt`를 고치되 첫 줄 주석은 그대로 둡니다. 직접 쓴 문장만 넣습니다.
- 엔진 코드는 그 단계 옆 단위 테스트에 한 줄을 더합니다.

## 다시 만들기와 ctest

```sh
build/mac/hdrulec lint src/core/rules -dicts dicts 2>&1 | tail -1
build/mac/hdrulec dump src/core/rules -hash > src/core/rules/program.hash
build/mac/hdrulec cpp src/core/rules -local 0-499 -globals -o src/core/rules/compiled_gen.cpp
cmake --build build/mac
H=$(mktemp -d); (cd build/mac && HOME=$H HDTRANS_DICTS="$PWD/../../dicts" ctest -j8 -E testdata)
```

`HDTRANS_DICTS`가 없으면 사전을 쓰는 테스트가 건너뜁니다.

## 판정

- 기준은 커밋을 고정한 별도 worktree의 빌드입니다. 계속 바뀌는 `main`을 기준으로 쓰지 않습니다.
- 개발 챕터와 held-out 챕터를 따로 비교하고 따로 보고합니다.
- 바뀐 줄은 Sonnet이 모두 보고, Sonnet이 나빠짐이나 같음이라 한 줄만 GPT(high)가 다시 봅니다.
- 두 모델이 모두 나빠졌다고 한 줄이 있으면 원인을 되돌리거나 좁힙니다. 남기면 커밋 메시지에 이유를 적습니다.
- 챕터 본문, 번역, 판정 파일은 비공개 저장소에만 둡니다.

## 여러 줄을 한꺼번에: 개선 루프

```
측정 → 추적 → 분류 → 고치기 → 확인 → 합치기 → 다시 측정
                                         ↑ 되돌린 것의 근본 원인
```

1. 측정: 모델이 줄마다 `line ja out verdict cause span fix`를 적은 판정 파일을 만듭니다(비공개 저장소).
2. 추적: `tools/attribute.py`가 판정한 때와 출력이 같은 줄에서, 나쁜 구간을 고른 층과 규칙을 붙입니다.
3. 분류: 위 표의 갈래로 나눕니다. 이미 받아들일 만한 번역은 뺍니다.
4. 고치기: 갈래나 규칙 파일별로 worktree와 브랜치를 나눠 동시에 고칩니다. 자기 파일과 자기 규칙의 테스트 행만 고칩니다.
5. 확인: 묶음마다 `ctest`와 판정을 한 번씩. 깊게 반복하지 않습니다.
6. 합치기: 브랜치를 하나씩 합치고 그때마다 생성 파일을 다시 만들고 `ctest`를 돌립니다. 다 합친 결과도 한 번 판정합니다.
7. 되돌린 수정은 원인별로 묶습니다. 태그 누락, 짧은 문맥 창, 뒤 층이 앞 결정을 덮음, 조립 문제, 더 긴 키가 규칙을 가림이
   흔한 원인이고 다음 바퀴에서 먼저 고칩니다.

## 사례

| 증상 | 원인 | 고친 것 |
|---|---|---|
| 競走で勝った → 경주로 | L1276을 좁혔는데 競走에 경기 코드가 없음 | 競走, レース `sem`에 `VRw` |
| 君の分のお菓子 → 너의분의 | 수량 뒤용 붙여 쓰는 행만 있음 | `分の 몫의` 행 + `L 5089 \|\|1 S-1== "の"` |
| 帰らなければと思う → 않으면이라고 | 조건 뒤 인용을 다루는 행이 없음 | 어미 행 `なければと`, L4664에 ば 분기 |
| 行けたらと思う → 갈 수 있다고 | rewrites `QuoteTo`가 たらと를 인용으로 바꿈 | `grammar.cpp` 사고 동사 앞은 `-면 하고` |
| こうなるんだ → 이러한 이다 | 형용동사 こう + 어미 なる로 분석 | `constraints.cpp` `kou-naru` |
| お仕置き → 징계 오고 | お仕置き가 없어 き를 来로 읽음 | `words.tsv` お仕置き `벌` |
