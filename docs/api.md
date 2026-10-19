# API

hdTrans는 하나의 정적 라이브러리(`hdTransCore`)와 그 위의 얇은 프런트엔드로 되어 있습니다.

| 프런트엔드 | 소스 | 인터페이스 |
|---|---|---|
| CLI `hdtrans` | `src/cli/` | 표준 입출력, UTF-8 |
| `hdTransEngine` (dll/so/dylib) | `src/cshared/` | C API, UTF-8 (`include/core/hdtrans.h`) |
| `J2KEngine.dll` | `src/j2kengine/` | 이지트랜스 호환 `J2K_*`, Shift-JIS/CP949 또는 UTF-16 |
| WebAssembly | `src/wasm/` | C API + Emscripten 파일 시스템 |
| C++ | `include/core/engine.h` | `hdTrans::Engine`, `hdTrans::Session` |

## 명령줄 (hdtrans)

표준 입력(또는 `-in FILE`)의 UTF-8을 읽어 번역을 표준 출력에 씁니다. 기본으로 입력 전체가 한 세션,
한 문서이며, 출력 줄은 `\n`으로 잇고 끝에 줄바꿈을 붙이지 않습니다. `-each`와 `-raw`는 줄마다 번역하고
각 줄 뒤에 `\n`을 붙입니다.

```
hdtrans [-config FILE] [-dicts DIR,...] [-userdict FILE] [-ehnd DIR] [-each | -raw] [-reference] [-verify-dicts] [-compile-dicts | -compile-snapshot FILE | -no-cache] [-why] [-select-shadow] [-misread] [-no-cg] [-cg-learned] [-stages DIR] [-rule-counts FILE] [-in FILE]
```

| 옵션 | 설명 |
|---|---|
| `-config FILE` | 기본 `user/hdtrans.config.yaml` 대신 읽을 설정 파일 |
| `-dicts DIR[,DIR...]` | 불러올 사전 폴더 (순서대로, 뒤가 앞을 덮어씀). 기본값은 데이터 루트의 `dicts`, 없으면 현재 폴더의 `dicts`. 첫 항목이 `FILE.hdsnap`이면 폴더의 열 파일 대신 그 [컴파일한 사전](dictionary-files.md#컴파일한-사전-hdsnap)을 씀 |
| `-userdict FILE` | 사용자 사전 파일로, 불러오지 못하면 실패. 기본값은 데이터 루트의 `user/userdict.tsv`(없으면 쓰지 않고, 불러오지 못하면 경고 후 빼고 번역), 빈 값이면 쓰지 않음 |
| `-in FILE` | 표준 입력 대신 FILE을 읽음 |
| `-ehnd DIR` | Ehnd 폴더 DIR의 필터, SkipLayer, 사용자 사전을 씀. 기본으로 쓰지 않으며 설정 파일로도 켤 수 있음. 건너뛴 줄은 표준 오류에 씀 ([ehnd-compat.md](ehnd-compat.md)) |
| `-each` | 줄마다 새 세션 |
| `-raw` | 줄마다 단어 파이프라인만으로 번역 |
| `-reference` | 번역 개선 규칙을 끈 기준 모드 |
| `-verify-dicts` | 원본 해시와 모든 캐시 세그먼트를 검사 |
| `-compile-dicts` | 사전 캐시를 컴파일, 검사하고 종료. 캐시 경로 또는 `up to date`를 출력 |
| `-compile-snapshot FILE` | 폴더의 열 파일을 FILE(`.hdsnap`)에 컴파일하고 종료. FILE을 출력 |
| `-no-cache` | 캐시 없이 TSV만 읽음 (`-compile-dicts`, `-compile-snapshot`과 함께 쓸 수 없음) |
| `-why` | 단어 선택과 shadow 점수를 표준 오류에 씀 |
| `-select-shadow` | 번역은 바꾸지 않고 가장 짧은 대상(`shortest-target`)으로 점수를 매김. `-why`에 나타남 |
| `-misread` | 오독 필터(`misreadFilter`)를 켬: 모델이 오독으로 점수를 매긴 선택 후보를 대안으로 바꿈 ([misread-classifier.md](misread-classifier.md)) |
| `-no-cg` | CG-lite 손 규칙(`cgRules`, 기본 켬)을 끔: 오독 결정의 후보를 지우거나 고르는 규칙표 |
| `-cg-learned` | 학습 규칙(`cgLearned`, 기본 끔)도 켬 |
| `-stages DIR` | 단계 기록을 DIR에 씀 ([testing.md](testing.md#한-입력-살펴보기-단계-기록--stages)) |
| `-rule-counts FILE` | 선택 규칙마다 실행 횟수와 참이 된 횟수를 FILE에 TSV로 씀: `kind`(L, G), `index`(전역 규칙은 PRIO), `pumsa`, `number`, `ugan`, `runs`, `fires` ([rule-language.md](rule-language.md#사용-횟수)) |

종료 코드: 0 성공, 1 사전을 못 불러옴, 입력 읽기 실패, 캐시 오류, 명시한 설정 파일 없음, 2 알 수 없는 옵션(사용법 출력).

`-why`의 표준 오류 출력은 탭으로 구분한 줄입니다. `WHY-LINE <줄>`이 입력 줄마다 나오고, 단어마다
`WHY <줄> <청크> <문장> <토큰>:<시작> <표면형> <선택> <출처> <id> <번역> <직접 선택> <출처> <id> <층> <규칙> <어미 층> <어미 규칙>`,
그 아래에 `SHADOW` 줄이 붙습니다. 층과 규칙은 후보와 어미를 정한 실행 층과 규칙 파일:규칙(`local`
`particles.txt:L925`, 전역 규칙은 `파일:GPRIO`)입니다([pipeline-layers.md](pipeline-layers.md#누가-정했는지--why)). 탭, 줄바꿈, 백슬래시는 `\t`, `\n`, `\\`로 이스케이프합니다.

| 환경 변수 | 설명 |
|---|---|
| `HDTRANS_CACHE_DIR` | 캐시 폴더 (절대 경로). 빈 값, 상대 경로(경고 출력), `off`(대소문자 무관)는 캐시를 끔 |
| `HDTRANS_SNAPSHOT=off` | 캐시를 끔 |
| `HDTRANS_VERIFY_DICTS=1` | `-verify-dicts`와 같음 |
| `HDTRANS_DRIVER_DUMP=DIR` | `-stages DIR`과 같음 |

### 설정 파일

프로그램 폴더에 `hdTrans/` 디렉터리가 있으면 그곳이 데이터 루트이고, 없으면 프로그램 폴더 자체입니다.
이름은 대소문자를 가리지 않습니다. 사전은 `dicts/`, 사용자 파일은 `user/`에 두며, 옛 배치의 프로그램 옆
`userdict.tsv`와 `hdtrans.config.yaml`도 지원합니다.

데이터 루트의 `user/hdtrans.config.yaml`을 시작할 때 읽습니다. DLL은 `hdTrans_Reload`나
`J2K_ReloadUserDict`에서도 다시 읽습니다. 파일이 없으면 기본값을 쓰며 Ehnd는 자동으로 켜지지 않습니다.
배포본의 `user/`에는 견본만 있습니다. [`hdtrans.config.example.yaml`](../hdtrans.config.example.yaml)을
같은 폴더의 `hdtrans.config.yaml`로 복사해 고칩니다. 상대 경로는 설정 파일의 폴더 기준이며,
CLI 플래그가 파일보다 우선합니다.

| 키 | 값과 기존 옵션 |
|---|---|
| `dicts` | 경로 하나 또는 경로 목록, `-dicts`처럼 순서대로 읽음 |
| `userdict` | 사용자 사전 경로, `-userdict`와 같음 |
| `ehnd` | 경로 또는 `true`(실행 파일이나 DLL 옆 `Ehnd`), `false`나 생략은 꺼짐. `-ehnd`와 같은 필터와 사용자 사전을 함께 적용 |
| `cache` | 경로 또는 `off`, `HDTRANS_CACHE_DIR`에 적용. `-no-cache`가 우선 |
| `cg` | bool, 기본 `true`. `false`는 `-no-cg` |
| `misread` | bool, 기본 `false`. `true`는 `-misread` |
| `reference` | bool, 기본 `false`. `true`는 `-reference` |

`cache`를 지우거나 DLL을 종료하면 적용 전 환경변수를 복원합니다. `HDTRANS_SNAPSHOT=off`는 그대로 유효합니다.
알 수 없는 키와 잘못된 값은 건너뛰고, 문법 오류 앞의 정상 항목은 유지합니다. 진단은 CLI의 표준 오류,
C API의 `hdTrans_UserDictError`, J2K의 디버거 출력으로 알립니다.
C API의 비어 있지 않은 `dirs`와 명시적인 `hdTrans_OpenEhnd`는 파일보다 우선합니다.
WebAssembly에도 같은 규칙을 적용하며 `/user/hdtrans.config.yaml`을 쓸 수 있습니다.

## 번역 창 (hdTransGUI.exe, Windows)

단문 번역 창과 문서 번역 창이 있습니다. 실행 파일 옆의 `hdTrans\`(없으면 실행 파일 폴더)에서 사전, 사용자 사전,
설정을 엔진을 처음 불러올 때 읽습니다. 문서 번역은 Shift-JIS, UTF-8(BOM 유무 무관), UTF-16LE(BOM)를 읽고
UTF-8 BOM으로 저장합니다. 문서 전체를 한 번에 번역하므로 문맥이 이어지고, **중단**은 지금 문서를 마친 뒤
결과를 저장하지 않습니다.

## C API (hdTransEngine)

헤더: `include/core/hdtrans.h`. Windows에서 DLL을 쓰는 쪽은 `HDTRANS_USE_DLL`을 정의합니다.

- 문자열은 모두 UTF-8입니다.
- 프로세스에 엔진 하나, 세션 하나입니다. 앞선 호출에서 찾은 이름과 문맥이 이후 번역에 반영됩니다.
- 호출은 스레드 안전하지만 재진입은 안 됩니다. 라이브러리를 내리기 전에 모든 호출을 끝내야 합니다.
- 초기화, 다시 읽기, 종료는 대기 중인 번역보다 먼저 처리됩니다.
- 데이터 루트의 `user/userdict.tsv`를 사전 폴더 다음에 읽고, 바뀌면 자동으로 다시 읽습니다
  ([dictionary-files.md](dictionary-files.md#userdicttsv)). 불러오지 못하면 빼고 시작합니다(`hdTrans_UserDictError`).
- Ehnd는 설정 파일의 `ehnd`나 `hdTrans_OpenEhnd`로 켭니다. 켠 폴더가 바뀌면 함께 다시 읽습니다
  ([ehnd-compat.md](ehnd-compat.md)).
- Windows에서 프로세스 종료 시에는 엔진을 정리하지 않고 OS에 맡기며, `FreeLibrary`로 내릴 때는 진행 중인
  다시 읽기를 기다리지 않습니다. 깔끔하게 정리하려면 `hdTrans_Terminate`를 부릅니다.

| 함수 | 설명 |
|---|---|
| `int hdTrans_Initialize(const char* dirs)` | 사전 폴더를 불러옴. `dirs`는 NUL로 구분하고 NUL 두 개로 끝나는 목록이며 뒤 폴더가 앞을 덮어씀. 첫 항목은 [컴파일한 사전](dictionary-files.md#컴파일한-사전-hdsnap) `.hdsnap`일 수 있음. 0 성공, -1은 사전을 하나도 못 불러왔거나 동시에 진행된 Terminate, Initialize, Reload가 이 불러오기를 취소함 |
| `void hdTrans_Terminate(void)` | 세션과 사전 해제. 여러 번 불러도 되고, 다시 Initialize할 수 있음 |
| `int hdTrans_Translate(const char* text, size_t n, char** out, size_t* outn)` | `text[0..n)`(`\n`으로 구분한 한 줄 이상)을 프로세스 세션에서 번역. `*out`에 NUL로 끝나는 버퍼, `*outn`에 길이(NUL 제외). 0 성공, -1 초기화 전, 종료 후 |
| `int hdTrans_Reset(void)` | 세션이 앞선 텍스트에서 배운 이름과 문맥을 잊어 다음 번역이 처음처럼 시작함. 0 성공, -1 초기화 전, 종료 후 |
| `int hdTrans_Reload(void)` | 설정 파일과 사전 폴더, 사용자 사전, 켜 둔 Ehnd 폴더를 다시 불러옴. 0 성공, -1 초기화 전, 종료 후, 실패(`userdict.tsv`를 불러오지 못한 경우 포함, 이전 사전 유지) |
| `int hdTrans_UserDictError(char** out, size_t* outn)` | 설정 파일 진단과 마지막 사용자 사전 읽기 오류(오류마다 한 줄, 없으면 빈 문자열)를 `hdTrans_Translate`처럼 버퍼로 돌려줌. 0 성공, -1 초기화 전, 종료 후 |
| `int hdTrans_OpenEhnd(const char* dir)` | Ehnd 폴더 `dir`의 필터와 사용자 사전을 이후 번역에 씀(NULL이나 빈 값은 뗌). `userdict.tsv`처럼 자동으로 다시 읽음. 0 성공, -1 초기화 전, 종료 후, 폴더를 못 읽음 |
| `int hdTrans_FilterErrors(char** out, size_t* outn)` | 필터 파일과 Ehnd 폴더에서 건너뛴 줄(`파일:줄: 이유`, 줄마다 하나)을 버퍼로 돌려줌. 0 성공, -1 초기화 전, 종료 후 |
| `void hdTrans_FreeMem(void* p)` | `hdTrans_Translate`가 돌려준 버퍼 해제 |
| `const char* hdTrans_Version(void)` | 라이브러리 버전 문자열 |

```c
#include "core/hdtrans.h"

if (hdTrans_Initialize("dicts\0user\0") != 0) return 1;
const char* text = "猫が好きです。";
char* out = NULL;
size_t outn = 0;
if (hdTrans_Translate(text, strlen(text), &out, &outn) == 0) {
    fwrite(out, 1, outn, stdout);
    hdTrans_FreeMem(out);
}
hdTrans_Terminate();
```

C API는 사전 캐시를 읽기만 하고 새로 저장하지 않습니다([dictionary-files.md](dictionary-files.md#캐시)).

## J2K 함수 (J2KEngine.dll)

이지트랜스의 `J2KEngine.dll`과 같은 이름의 함수를 `__stdcall`로 내보냅니다(`src/j2kengine/j2kengine.def`).
Windows 전용이며, 이지트랜스를 불러오는 도구는 대부분 32비트이므로 x86 빌드를 씁니다.

- 사전은 데이터 루트의 `dicts/` 폴더와 `user/userdict.tsv`에서 읽습니다. 호스트가 넘기는 사전 경로는 무시합니다.
  `userdict.tsv`는 바뀌면 자동으로 다시 읽습니다([dictionary-files.md](dictionary-files.md#userdicttsv)). 불러오지 못하면
  빼고 시작하며 이유를 디버거 출력(`OutputDebugString`)에 씁니다.
- Ehnd는 설정 파일의 `ehnd`로 켭니다. 켠 폴더가 바뀌면 함께 다시 읽으며 건너뛴 줄은
  디버거 출력에 씁니다 ([ehnd-compat.md](ehnd-compat.md)).
- 프로세스 종료 시에는 엔진을 정리하지 않고, `FreeLibrary`로 내릴 때는 진행 중인 다시 읽기를 기다리지 않습니다.
- Shift-JIS(932), CP949(949), UTF-16 변환은 OS 함수로 이 경계에서만 합니다. 엔진 안에는 코드 페이지
  표가 없습니다.
- 반환 버퍼는 프로세스 힙에 할당하며 `J2K_FreeMem`으로 해제합니다.
- 호출은 스레드 안전하지만 재진입은 안 됩니다.

| 함수 | 동작 |
|---|---|
| `J2K_Initialize(char* dicPath)` | 사전을 불러옴. 1 성공(이미 초기화되어 있어도 1), 0 실패, 취소 |
| `J2K_InitializeEx(char* dicPath, char* mainDir)` | `J2K_Initialize`와 같음 |
| `J2K_Terminate()` | 세션과 사전 해제, 1 |
| `J2K_TranslateMMNT(HWND, char* text)` | Shift-JIS 입력, CP949 출력 |
| `J2K_TranslateMMNTW(HWND, const wchar_t* text)` | UTF-16 입력, UTF-16 출력 |
| `J2K_TranslateMM`, `J2K_TranslateMMEx`, `J2K_TranslateChat` | `J2K_TranslateMMNT`와 같음 |
| `J2K_TranslateFM(HWND, char* fileName)` | 파일 번역 없음, null |
| `J2K_FreeMem(void*)` | 번역 결과 해제 |
| `J2K_ReloadUserDict()` | 설정 파일과 사전, 켜 둔 Ehnd 폴더를 바로 다시 불러옴 |
| `J2K_StopTranslation` | 아무것도 하지 않고 1 |
| `J2K_SetField`, `J2K_SetHnj2han`, `J2K_SetDelJPN`, `J2K_SetJWin`, `J2K_SetPriorDict`, `J2K_GetJ2KMainDir` | 호환용, 아무것도 하지 않음 |
| `J2K_GetPriorDict` | null |
| `J2K_SetProperty`, `J2K_GetProperty` | 0 |

`?GetJ2KMainDir@@YA?AVCString@@XZ`는 `J2K_GetJ2KMainDir`의 별칭입니다.

## WebAssembly

`emcmake cmake --preset wasm`으로 빌드한 `hdTransEngine.js`(ES6 모듈, `MODULARIZE`)와
`hdTransEngine.wasm`입니다. C API를 그대로 내보내며, 사전 파일은 Emscripten 파일 시스템(`FS`)에 써서
`hdTrans_Initialize`에 그 폴더를 줍니다. TSV를 컴파일하는 동안의 메모리가 WebAssembly 메모리에 그대로 남으므로,
열 사전은 네이티브 CLI로 [컴파일한 사전](dictionary-files.md#컴파일한-사전-hdsnap)으로 주는 편이 좋습니다:
`hdTrans_Initialize("/dicts.hdsnap\0/dicts\0")`(나머지 파일은 `/dicts`에).

- 내보내는 함수: `hdTrans_Initialize`, `hdTrans_Terminate`, `hdTrans_Translate`, `hdTrans_Reset`,
  `hdTrans_Reload`, `hdTrans_UserDictError`, `hdTrans_FreeMem`, `hdTrans_Version`, `hdTrans_Load`, `malloc`, `free`
- 런타임 메서드: `FS`, `ccall`, `cwrap`, `UTF8ToString`, `stringToUTF8`, `lengthBytesUTF8`, `getValue`, `HEAPU8`
- 사용자 사전은 `/user/userdict.tsv`이며 옛 `/userdict.tsv`도 지원합니다. 스레드가 없어 자동으로 다시 읽지 않으므로, 파일을
  쓴 뒤 `hdTrans_Reload`를 부르거나 `hdTrans_Terminate` 다음 `hdTrans_Initialize`로 다시 불러옵니다.
- `hdTrans_Load(name, data, n)`(사전 파일을 버퍼로 넘기는 입구, `src/wasm/main.cpp`)는 아직 구현되지 않아
  -1을 돌려줍니다.
- 웹 데모(`web/`)가 이 모듈을 씁니다([web.md](web.md)).

```js
import createEngine from './hdTransEngine.js';

const engine = await createEngine();
engine.FS.mkdir('/dicts');
engine.FS.writeFile('/dicts/base.tsv', baseTsvBytes);
engine.ccall('hdTrans_Initialize', 'number', ['string'], ['/dicts\0']);

const text = '学生です。';
const out = engine._malloc(8);
engine.ccall('hdTrans_Translate', 'number', ['string', 'number', 'number', 'number'],
             [text, engine.lengthBytesUTF8(text), out, out + 4]);
const ptr = engine.getValue(out, '*');
const result = engine.UTF8ToString(ptr, engine.getValue(out + 4, 'i32'));
engine._hdTrans_FreeMem(ptr);
engine._free(out);
```

CI의 `tools/wasm_smoke.mjs`가 같은 순서로 동작을 확인합니다.

## C++ API

`include/core/engine.h`:

- `hdTrans::Engine(bool reference = false, CacheWrite cacheWrite = CacheWrite::Sync, UserReload userReload = UserReload::Off)`:
  불러온 사전. 여러 세션과 스레드가 함께 씁니다. `Open(dirs, verifyDicts, userFile)`, `Reload()`, `loaded()`,
  `Dictionaries()`, `UserFileError()`, `DetachReload()`. `userFile`은 폴더 다음에 읽는 사용자 사전 파일(빈 값이나
  없는 파일이면 없음)이며, 불러오지 못하면 빼고 열고 `UserFileError()`가 이유를 돌려줍니다.
  `CacheWrite::Off`는 캐시 세대를 저장하지 않고, `Sync`는 `Open`이 끝나기 전에 저장을 시도합니다.
  `UserReload::Auto`는 번역을 시작할 때 `userFile`과 Ehnd 폴더를 1초에 한 번까지 확인해, 바뀌면 백그라운드에서 다시
  불러와 바꿉니다. 엔진을 없애면 진행 중인 다시 읽기가 끝나기를 기다립니다. `DetachReload()` 뒤에는 기다리지
  않고, 다시 읽기는 스스로 끝납니다(라이브러리를 내릴 때).
  `OpenEhnd(dir)`은 Ehnd 폴더의 필터와 사용자 사전을 그 뒤에 만들거나 `Reset`한 세션에 씁니다(빈
  경로는 뗌, 폴더를 못 읽으면 `false`, `Open` 전에는 폴더 이름만 정함). `Reload()`와 `UserReload::Auto`의
  다시 읽기는 Ehnd 폴더도 함께 읽습니다. `FilterErrors()`는 필터 파일과 Ehnd 폴더에서 건너뛴 줄을
  `파일:줄: 이유`로, `Filters()`는 현재 필터를 돌려줍니다 ([ehnd-compat.md](ehnd-compat.md)).
- `hdTrans::Session(const Engine&, bool reference = false, bool cg = true, bool misread = false)`: 문맥을 기억하는 번역. 한 번에 한 스레드에서
  씁니다. `Translate(text)`는 `\n`으로 구분한 한 줄 이상을 번역하고(`\r`은 유지), 64 KiB가 넘는 줄은
  16 KiB 이하 조각으로 나눠 번역합니다. `Reset()`은 기억을 지우고 엔진의 현재 사전을 가져옵니다.
  `UserReload::Auto` 엔진의 세션은 번역을 시작할 때 엔진에 새 사전이 있으면 기억을 지우고 그 사전을 씁니다.
