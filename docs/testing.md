# 테스트

## ctest

`ctest --preset <프리셋>`이 실행하는 테스트:

| 이름 | 내용 |
|---|---|
| `unit` | 단위 테스트 `hdtrans_tests` |
| `tools.selfbase` | `tools/snapshot.sh`와 `compare.sh`를 작은 사전으로 두 번 실행해 결과가 같은지 확인 (Unix) |
| `examples` | `examples/` 번역 확인. `HDTRANS_DICTS`가 없으면 건너뜀 |
| `rules.cases` | `tests/rules/cases.tsv`의 규칙 사례를 `dicts/`로 번역해 확인 ([아래](#규칙-사례-testsrulescasestsv)) |
| `testdata` | 테스트 데이터의 기대 출력과 비교 ([테스트 데이터](#테스트-데이터-비공개-저장소)). `HDTRANS_TESTDATA`가 없으면 건너뜀 |
| `host.admin_priority` | C API에서 초기화·다시 읽기·종료가 대기 중인 번역보다 먼저 처리되는지 확인 |
| `host.j2k` | `J2KEngine.dll`과 `hdTransEngine.dll`의 초기화·종료와 동시 번역 확인 (Windows) |

```sh
HDTRANS_DICTS=$PWD/dicts ctest --preset mac
```

## 단위 테스트

`tests/unit/`에 있으며, `src/core/`와 같은 단계별 폴더(`dict/`, `tokenizer/`, `word/` 등)로 나뉩니다. 프레임워크 없이
`tests/unit/hdtest.h`의 `HD_TEST`, `HD_CHECK`, `HD_REQUIRE`, `HD_SKIP`을 씁니다. 실제 입력을 쓰는 표
형식 테스트이며, 픽스처는 `tests/unit/**/testdata/`의 작은 파일입니다.

```sh
build/mac/hdtrans_tests            # 전체
build/mac/hdtrans_tests userdict   # 이름에 userdict가 들어간 테스트만
```

러너는 `HOME`, `LOCALAPPDATA`, `XDG_CACHE_HOME`을 임시 폴더로 바꾸고, `HDTRANS_CACHE_DIR`가 없으면 임시
캐시 폴더를 지정합니다. 실행 중 사용자 캐시 폴더가 바뀌면 실패합니다. `tests/runner.sh`는 이 러너
동작 자체를 확인합니다.

| 환경 변수 | 설명 |
|---|---|
| `HDTRANS_REQUIRE_ALL=1` | 건너뛴 테스트가 하나라도 있으면 실패 |
| `HDTRANS_TEST_EXCLUDE=a,b` | 쉼표로 구분한 이름의 테스트를 건너뜀 |
| `HDTRANS_CACHE_DIR` | 테스트가 쓸 캐시 폴더 (절대 경로) |

## 규칙 사례 (tests/rules/cases.tsv)

규칙 프로그램(`src/core/rules/*.txt`)의 단어 선택을 고정하는 표입니다. 열은 `rule input expected kind layer`이고
입력 한 줄을 한 세션으로 번역합니다(`-each -why`). 실패는 `layer`별로 묶어 보입니다.

- `rule`: 사례가 겨냥하는 규칙(`L1276`, 전역 규칙은 PRIO로 `G330`)
- `input`: 직접 쓴 짧은 일본어 문장
- `expected`: 번역 전체, 또는 `rule:L1276=<번역>`(그 줄의 어떤 단어를 지역 규칙 1276이 그 번역으로 골랐음, `-why` 기준)
- `kind`: `fix`(고친 규칙이 이제 맞게 고르는 문장), `keep`(고치기 전 선택이 맞아서 그대로 남아야 하는 문장),
  `current`(지금 동작을 그대로 적어 둔 문장)
- `layer`: 그 규칙이 도는 실행 층([architecture.md](architecture.md)의 `local`, `local-ending`, `global-stem`,
  `global-last`, `global-ending`)

```sh
tools/rule-cases.sh build/mac/hdtrans dicts            # 다르면 규칙·입력·기대값·실제값을 보이고 실패
tools/rule-cases.sh build/mac/hdtrans dicts --accept   # current 행의 번역 전체 기대값을 새 출력으로 바꿈
```

규칙을 고칠 때는 `fix` 행과 `keep` 행을 하나 이상씩 더하고, 커밋 전에 표 전체와 `examples/`를 돌립니다.

## 예제 문서 (examples/)

`examples/`에는 짧은 일본어 문서와 번역 결과가 있습니다.

- `<case>.ja.txt`: 첫 줄은 사례를 설명하는 `#` 줄, 그다음은 한 문서로 번역할 입력
- `<case>.ko.txt`: 같은 `#` 줄, 그다음은 검토를 마친 현재 엔진 출력

```sh
tools/examples.sh build/mac/hdtrans dicts            # *.ko.txt와 다르면 실패
tools/examples.sh build/mac/hdtrans dicts --accept   # 새 출력을 *.ko.txt로 받아들임
```

출력은 `build/examples/`에 생깁니다. 사전 인자가 비어 있으면 77로 끝나며, ctest는 이를 건너뜀으로
처리합니다.

## 한 입력 살펴보기: 단계 기록 (-stages)

```sh
printf '%s' '学生です。' | build/mac/hdtrans -dicts dicts -stages stages
```

입력 하나가 각 파이프라인 단계를 지나며 남긴 기록을 `stages/input.<TAG>.got.tsv`에 씁니다.
`#LINE` 머리줄 아래에 이스케이프된 TSV 레코드가 있습니다. `HDTRANS_DRIVER_DUMP=stages`로도 켤 수
있습니다. `-raw`를 붙이면 단어 파이프라인만, `-each`를 붙이면 줄마다 새 세션으로 기록합니다.
`-why`는 단어 선택과 shadow 점수를 표준 오류로 씁니다([api.md](api.md#명령줄-hdtrans)).

오독 필터의 기록은 문장의 선택이 끝난 뒤(S와 같은 시점)에 남습니다. `MI`는 체인의 단어마다 위치, 표면형,
앞뒤 글자, 줄 첫머리 여부, 선택과 출처, `MC`는 단어의 후보(W)와 어미 후보(E)마다 품사·의미·문법 코드,
사용 가능 여부, 한국어와 사전형, `MS`는 소리 읽기, `MF`는 결정의 후보마다 내장 모델 점수와 특징입니다.
`tools/misread/check.py`는 MI·MC·MS로 같은 특징을 다시 만들어 MF와 비교합니다([misread-classifier.md](misread-classifier.md)).

## 자체 기준선

```sh
tools/snapshot.sh <사전 폴더> <이름>                      # 빌드한 CLI의 출력을 기록
tools/compare.sh <runA> <runB>                            # 두 기록의 차이
tools/explain.sh <binA> <binB> <사전 폴더> '学生です。'   # 처음 달라지는 단계와 레코드
```

- `snapshot.sh`는 빌드하지 않고, 이미 빌드한 CLI를 기본 번역 설정과 기록마다 새 캐시로 실행합니다.
  같은 이름의 기록이 있으면 거부합니다.
- 기록은 `${HDTRANS_RUNS:-../hdtrans-data/cpp-runs}` 아래의 바꾸지 않는 폴더입니다. 입력과 출력의 복사본,
  커밋·바이너리·사전의 SHA-256, 세트별 시간(`sets.tsv`)이 들어 있습니다.
- 입력: `HDTRANS_DATA`(기본 `../hdtrans-data`), `HDTRANS_INPUTS`(기본 `$HDTRANS_DATA/hdtrans-golden`,
  최상위 `.in` 파일만). `*.raw.in`은 `-raw`, `*.each.in`은 `-each`, 나머지는 문서로 번역합니다.
  `lyrics/cases.tsv`의 가사는 곡별로 묶이고, `syosetu/*.ja.txt`는 문서로 번역합니다.
- `HDTRANS_BIN`: 사용할 CLI (기본 `build/mac/hdtrans`).
- `compare.sh`는 기록 이름이나 폴더 경로를 받아 세트, 줄, 입력, A, B와 세트별·전체 개수를 출력합니다.
- `compare.sh`와 `explain.sh`의 종료 코드: 0 같음, 1 다름, 2 잘못된 입력.

## 테스트 데이터 (비공개 저장소)

저작권이 있는 글로 하는 엔드투엔드 테스트의 입력과 기대 출력은 비공개 저장소 `sokcuri/hdTrans-data`에
있습니다. 이 저장소에는 넣지 않습니다. 그 본문이나 번역을 이 저장소의 파일, 커밋 메시지, 이슈, 풀 리퀘스트에
옮기지 않습니다. 접근 권한이 없으면 아래 테스트는 건너뜁니다.

- 출처마다 폴더 하나(`syosetu/`, 앞으로 `corpus/`, `lyrics/`, `eroge/` 등)이고, 하위 폴더는 없습니다.
- `<set>.<NNN>.ja.txt`: 첫 줄은 작품, 작가나 권리자, 원문 URL을 적은 `#` 줄, 그다음은 입력
- `<set>.<NNN>.ko.txt`: 같은 `#` 줄, 그다음은 입력 전체를 한 문서로 기본 설정으로 번역한 엔진 출력

```sh
export HDTRANS_TESTDATA=/path/to/hdTrans-data                        # 받은 테스트 데이터 (절대 경로)
tools/e2e.sh --expected "$HDTRANS_TESTDATA" build/mac/hdtrans dicts            # 기대 출력과 비교
tools/e2e.sh --expected "$HDTRANS_TESTDATA" build/mac/hdtrans dicts --accept   # 새 출력을 받아들임
tools/e2e.sh <cliA|runA> build/mac/hdtrans dicts                    # 두 빌드 비교
ctest --preset mac -R testdata                                       # --expected를 ctest로
```

- `e2e.sh --expected DATA CLI DICTS [--accept]`: `DATA/*/*.ja.txt`를 첫 줄을 빼고 한 문서로 번역해
  `build/e2e/<폴더>/<name>.ko.txt`에 쓰고, `DATA`의 `.ko.txt`(A)와 비교합니다. 달라진 문서마다 최대 20줄,
  전체 최대 200줄의 변경(파일 줄 번호, A, B)을 출력하고, 마지막 줄에 문서·줄 개수를 씁니다. `.ko.txt`가
  없는 문서는 모든 줄이 바뀐 것으로 셉니다. `--accept`는 달라진 `.ko.txt`를 새 출력으로 덮어씁니다.
- `e2e.sh A B DICTS [DATA]`: 같은 문서를 두 빌드로 번역해 `build/e2e/<폴더>/`(B)와
  `build/e2e/base/<폴더>/`(A)에 쓰고 비교합니다. A에는 CLI 대신 `snapshot.sh` 기록 폴더를 줄 수 있으며,
  그러면 입력이 같은 문서 세트를 A로 씁니다. `HDTRANS_DICTS_A`는 A에 다른 사전 폴더를 줍니다.
- `DATA`가 비었거나 없으면 `HDTRANS_TESTDATA`를 씁니다. 둘 다 없으면 77로 끝나며, ctest의 `testdata`는
  이를 건너뜀으로 처리합니다. `testdata`는 저장소의 `dicts/`를 씁니다.
- 종료 코드: 0 같음 또는 받아들임, 1 바뀜, 2 잘못된 인자·데이터, CLI 실패, 비교한 문서 없음, 77 테스트
  데이터 없음.

### 번역이 바뀌었을 때

번역 동작을 바꾸는 변경은 이유를 적은 별도 커밋으로 만듭니다. 그 커밋이 `main`에 들어가면, 그 커밋으로
빌드한 CLI와 `dicts/`로 `--accept`를 실행하고, 테스트 데이터 저장소에 엔진 커밋을 적어 커밋합니다.

### 챕터 더하기 (syosetu)

```sh
printf '# <제목> <화> (작가: <작가>) <URL>\n' > "$HDTRANS_TESTDATA/syosetu/syosetu.052.ja.txt"
tools/fetch-syosetu.sh                  # 첫 줄만 있는 파일에 본문을 받음
tools/e2e.sh --expected "$HDTRANS_TESTDATA" build/mac/hdtrans dicts --accept
```

`fetch-syosetu.sh [DIR]`(기본 `$HDTRANS_TESTDATA/syosetu`)는 첫 줄만 있는 `.ja.txt`마다 그 줄 끝의
小説家になろう URL에서 챕터를 받아, 챕터 제목과 단락마다 한 줄(루비 읽기 제거)을 덧붙입니다. 1초에 한 번
요청하고, 받지 못한 챕터는 보고하고 그대로 둡니다. 작품 제목과 작가는 小説家になろう API
(`https://api.syosetu.com/novelapi/api/?out=json&of=t-w&ncode=<ncode>`)의 `title`, `writer`입니다.

### CI

`.github/workflows/e2e.yml`은 풀 리퀘스트와 `main` 푸시마다 현재 커밋의 CLI만 빌드해 `--expected`로
비교합니다. 테스트 데이터는 Actions 비밀 `HDTRANS_TESTDATA_KEY`(테스트 데이터 저장소의 읽기 전용 배포 키)로
받습니다. 비밀을 쓸 수 없는 실행(포크의 풀 리퀘스트)은 알림만 남기고 건너뜁니다. 작업 요약에는 개수만,
로그에는 보고의 처음 몇 줄만 남깁니다. 종료 코드 1(바뀜)은 실패가 아니고 2만 실패입니다.

## 잘못된 출력 찾기 (detect-bad.sh)

```sh
tools/detect-bad.sh build/e2e/*/*.ko.txt    # 걸린 곳마다 한 줄: 파일:줄, 패턴, 짧은 구간
tools/detect-bad.sh -c build/e2e/*/*.ko.txt # 패턴별 개수만
```

한국어 출력에서 올바른 번역에는 나올 수 없는 연쇄를 찾습니다. 첫 줄이 `#`로 시작하면 건너뜁니다(e2e·예제
출력의 `#` 줄). 구간은 걸린 곳 앞뒤 몇 글자만 보여 줍니다. 종료 코드: 0 없음, 1 있음, 2 잘못된 인자.

| 패턴 | 찾는 것 |
|---|---|
| `ending-honorific` | 종결 어미 뒤에 붙은 군·씨·님·짱 (`했다님`, `있어요씨`) |
| `doubled-ending` | 종결 어미가 두 번 (`그렇네요군요`, `합니다입니다`) |
| `stem-eung` | 동사 어간 바로 뒤의 감탄사 응 (`하응`, `않응`) |
| `particle-after-punct` | `, . ! ? … ~` 뒤에 홀로 선 조사 (`! 와`, `, 는`). `가 주`(가다)와 `와?`(오다)는 뺌 |
| `residue` | 출력에 남은 일본어 문자나 엔진 표지 (`승용き`, `&AV@`, `(이)가`) |

맞는 출력도 걸릴 수 있는 검사(정확도 100%가 아님)이므로, 걸린 곳을 직접 확인한 뒤 고칩니다. 확인한 결과 모두
실제 오류인 패턴만 엔진 규칙(`src/core/word/constraints.cpp`의 제약 규칙)으로 옮깁니다.

### 원문과 함께 보기 (--source)

```sh
tools/detect-bad.sh --source build/e2e/syosetu.038.ja.txt build/e2e/syosetu.038.ko.txt   # 원문·번역 쌍
tools/detect-bad.sh -c --source A.ja.txt A.ko.txt B.ja.txt B.ko.txt                       # 패턴별 개수만
```

한국어만 보면 멀쩡해 보이는 오역(`かッハ……！` → `인가 하……!`)을 원문과 맞대어 찾습니다. 줄 번호가 맞는
일본어·한국어 파일 쌍을 받고, 각 줄을 같은 문장 부호(「」『』（）、。！？…‥―)에서 잘라 조각 수가 같을 때만
조각끼리 비교합니다. 첫 줄이 `#`이면 건너뜁니다. 종료 코드는 위와 같습니다.

| 패턴 | 찾는 것 |
|---|---|
| `sound-function` | 소리로 읽히는 일본어 조각(가나 2~5자, ー·～ 포함, 첫 글자가 아닌 っ·ッ가 있음, を로 시작하지 않음)이 기능어·조사 낱말이 든 한국어가 됨 (`かッハ` → `인가 하`, `だっは` → `다 는`). 다음 조각의 첫 음절 하나로 쓴 말더듬(`こっ、この` → `이, 이`)은 뺌 |

## 환경 변수 요약

| 변수 | 쓰는 곳 | 설명 |
|---|---|---|
| `HDTRANS_DICTS` | ctest `examples` | 예제 번역에 쓸 사전 폴더 |
| `HDTRANS_REQUIRE_ALL` | 단위 테스트 | `1`이면 건너뛴 테스트를 실패로 |
| `HDTRANS_TEST_EXCLUDE` | 단위 테스트 | 건너뛸 테스트 이름 (쉼표 구분) |
| `HDTRANS_DATA`, `HDTRANS_INPUTS`, `HDTRANS_RUNS`, `HDTRANS_BIN` | `snapshot.sh`, `compare.sh` | 입력·기록 위치와 CLI |
| `HDTRANS_TESTDATA` | `e2e.sh`, `fetch-syosetu.sh`, ctest `testdata` | 테스트 데이터 폴더 |
| `HDTRANS_DICTS_A` | `e2e.sh` | A 쪽 사전 |
| `HDTRANS_DRIVER_DUMP` | CLI | `-stages`와 같음 |
| `HDTRANS_CACHE_DIR`, `HDTRANS_SNAPSHOT`, `HDTRANS_VERIFY_DICTS` | 엔진 | 사전 캐시 ([dictionaries.md](dictionaries.md#캐시)) |
