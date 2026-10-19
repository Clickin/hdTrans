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
hdtrans [-dicts DIR,...] [-userdict FILE] [-each | -raw] [-reference] [-verify-dicts] [-compile-dicts | -compile-snapshot FILE | -no-cache] [-why] [-select-shadow] [-stages DIR] [-in FILE]
```

| 옵션 | 설명 |
|---|---|
| `-dicts DIR[,DIR...]` | 불러올 사전 폴더 (순서대로, 뒤가 앞을 덮어씀). 기본값은 실행 파일 옆 `dicts`, 없으면 현재 폴더의 `dicts`. 첫 항목이 `FILE.hdsnap`이면 폴더의 열 파일 대신 그 [컴파일한 사전](dictionaries.md#컴파일한-사전-hdsnap)을 씀 |
| `-userdict FILE` | 사용자 사전 파일로, 불러오지 못하면 실패. 기본값은 실행 파일 옆 `userdict.tsv`(없으면 쓰지 않고, 불러오지 못하면 경고 후 빼고 번역), 빈 값이면 쓰지 않음 |
| `-in FILE` | 표준 입력 대신 FILE을 읽음 |
| `-each` | 줄마다 새 세션 |
| `-raw` | 줄마다 단어 파이프라인만으로 번역 |
| `-reference` | 번역 개선 규칙을 끈 기준 모드 |
| `-verify-dicts` | 원본 해시와 모든 캐시 세그먼트를 검사 |
| `-compile-dicts` | 사전 캐시를 컴파일·검사하고 종료. 캐시 경로 또는 `up to date`를 출력 |
| `-compile-snapshot FILE` | 폴더의 열 파일을 FILE(`.hdsnap`)에 컴파일하고 종료. FILE을 출력 |
| `-no-cache` | 캐시 없이 TSV만 읽음 (`-compile-dicts`·`-compile-snapshot`과 함께 쓸 수 없음) |
| `-why` | 단어 선택과 shadow 점수를 표준 오류에 씀 |
| `-select-shadow` | 번역은 바꾸지 않고 가장 짧은 대상(`shortest-target`)으로 점수를 매김. `-why`에 나타남 |
| `-stages DIR` | 단계 기록을 DIR에 씀 ([testing.md](testing.md#한-입력-살펴보기-단계-기록--stages)) |

종료 코드: 0 성공, 1 사전을 못 불러옴·입력 읽기 실패·캐시 오류, 2 알 수 없는 옵션(사용법 출력).

`-why`의 표준 오류 출력은 탭으로 구분한 줄입니다. `WHY-LINE <줄>`이 입력 줄마다 나오고, 단어마다
`WHY <줄> <청크> <문장> <토큰>:<시작> <표면형> <선택> <출처> <id> <번역> <직접 선택> <출처> <id>`,
그 아래에 `SHADOW` 줄이 붙습니다. 탭·줄바꿈·백슬래시는 `\t`, `\n`, `\\`로 이스케이프합니다.

| 환경 변수 | 설명 |
|---|---|
| `HDTRANS_CACHE_DIR` | 캐시 폴더 (절대 경로). 빈 값, 상대 경로(경고 출력), `off`(대소문자 무관)는 캐시를 끔 |
| `HDTRANS_SNAPSHOT=off` | 캐시를 끔 |
| `HDTRANS_VERIFY_DICTS=1` | `-verify-dicts`와 같음 |
| `HDTRANS_DRIVER_DUMP=DIR` | `-stages DIR`과 같음 |

## C API (hdTransEngine)

헤더: `include/core/hdtrans.h`. Windows에서 DLL을 쓰는 쪽은 `HDTRANS_USE_DLL`을 정의합니다.

- 문자열은 모두 UTF-8입니다.
- 프로세스에 엔진 하나, 세션 하나입니다. 앞선 호출에서 찾은 이름과 문맥이 이후 번역에 반영됩니다.
- 호출은 스레드 안전하지만 재진입은 안 됩니다. 라이브러리를 내리기 전에 모든 호출을 끝내야 합니다.
- 초기화·다시 읽기·종료는 대기 중인 번역보다 먼저 처리됩니다.
- 라이브러리 옆의 `userdict.tsv`를 사전 폴더 다음에 읽고, 바뀌면 자동으로 다시 읽습니다
  ([dictionaries.md](dictionaries.md#userdicttsv)). 불러오지 못하면 빼고 시작합니다(`hdTrans_UserDictError`).
- Windows에서 프로세스 종료 시에는 엔진을 정리하지 않고 OS에 맡기며, `FreeLibrary`로 내릴 때는 진행 중인
  다시 읽기를 기다리지 않습니다. 깔끔하게 정리하려면 `hdTrans_Terminate`를 부릅니다.

| 함수 | 설명 |
|---|---|
| `int hdTrans_Initialize(const char* dirs)` | 사전 폴더를 불러옴. `dirs`는 NUL로 구분하고 NUL 두 개로 끝나는 목록이며 뒤 폴더가 앞을 덮어씀. 첫 항목은 [컴파일한 사전](dictionaries.md#컴파일한-사전-hdsnap) `.hdsnap`일 수 있음. 0 성공, -1은 사전을 하나도 못 불러왔거나 동시에 진행된 Terminate·Initialize·Reload가 이 불러오기를 취소함 |
| `void hdTrans_Terminate(void)` | 세션과 사전 해제. 여러 번 불러도 되고, 다시 Initialize할 수 있음 |
| `int hdTrans_Translate(const char* text, size_t n, char** out, size_t* outn)` | `text[0..n)`(`\n`으로 구분한 한 줄 이상)을 프로세스 세션에서 번역. `*out`에 NUL로 끝나는 버퍼, `*outn`에 길이(NUL 제외). 0 성공, -1 초기화 전·종료 후 |
| `int hdTrans_Reset(void)` | 세션이 앞선 텍스트에서 배운 이름과 문맥을 잊어 다음 번역이 처음처럼 시작함. 0 성공, -1 초기화 전·종료 후 |
| `int hdTrans_Reload(void)` | 같은 폴더와 `userdict.tsv`를 바로 다시 불러옴. 0 성공, -1 초기화 전·종료 후·실패(`userdict.tsv`를 불러오지 못한 경우 포함, 이전 사전 유지) |
| `int hdTrans_UserDictError(char** out, size_t* outn)` | 마지막으로 읽은 `userdict.tsv`를 불러오지 못한 이유(오류마다 한 줄, 불러왔거나 없으면 빈 문자열)를 `hdTrans_Translate`처럼 버퍼로 돌려줌. 0 성공, -1 초기화 전·종료 후 |
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

C API는 사전 캐시를 읽기만 하고 새로 저장하지 않습니다([dictionaries.md](dictionaries.md#캐시)).

## J2K 함수 (J2KEngine.dll)

이지트랜스의 `J2KEngine.dll`과 같은 이름의 함수를 `__stdcall`로 내보냅니다(`src/j2kengine/j2kengine.def`).
Windows 전용이며, 이지트랜스를 불러오는 도구는 대부분 32비트이므로 x86 빌드를 씁니다.

- 사전은 DLL 옆의 `dicts\` 폴더와 `userdict.tsv`에서 읽습니다. 호스트가 넘기는 사전 경로는 무시합니다.
  `userdict.tsv`는 바뀌면 자동으로 다시 읽습니다([dictionaries.md](dictionaries.md#userdicttsv)). 불러오지 못하면
  빼고 시작하며 이유를 디버거 출력(`OutputDebugString`)에 씁니다.
- 프로세스 종료 시에는 엔진을 정리하지 않고, `FreeLibrary`로 내릴 때는 진행 중인 다시 읽기를 기다리지 않습니다.
- Shift-JIS(932)·CP949(949)·UTF-16 변환은 OS 함수로 이 경계에서만 합니다. 엔진 안에는 코드 페이지
  표가 없습니다.
- 반환 버퍼는 프로세스 힙에 할당하며 `J2K_FreeMem`으로 해제합니다.
- 호출은 스레드 안전하지만 재진입은 안 됩니다.

| 함수 | 동작 |
|---|---|
| `J2K_Initialize(char* dicPath)` | 사전을 불러옴. 1 성공(이미 초기화되어 있어도 1), 0 실패·취소 |
| `J2K_InitializeEx(char* dicPath, char* mainDir)` | `J2K_Initialize`와 같음 |
| `J2K_Terminate()` | 세션과 사전 해제, 1 |
| `J2K_TranslateMMNT(HWND, char* text)` | Shift-JIS 입력, CP949 출력 |
| `J2K_TranslateMMNTW(HWND, const wchar_t* text)` | UTF-16 입력, UTF-16 출력 |
| `J2K_TranslateMM`, `J2K_TranslateMMEx`, `J2K_TranslateChat` | `J2K_TranslateMMNT`와 같음 |
| `J2K_TranslateFM(HWND, char* fileName)` | 파일 번역 없음, null |
| `J2K_FreeMem(void*)` | 번역 결과 해제 |
| `J2K_ReloadUserDict()` | `dicts\`와 `userdict.tsv`를 바로 다시 불러옴 |
| `J2K_StopTranslation` | 아무것도 하지 않고 1 |
| `J2K_SetField`, `J2K_SetHnj2han`, `J2K_SetDelJPN`, `J2K_SetJWin`, `J2K_SetPriorDict`, `J2K_GetJ2KMainDir` | 호환용, 아무것도 하지 않음 |
| `J2K_GetPriorDict` | null |
| `J2K_SetProperty`, `J2K_GetProperty` | 0 |

`?GetJ2KMainDir@@YA?AVCString@@XZ`는 `J2K_GetJ2KMainDir`의 별칭입니다.

## WebAssembly

`emcmake cmake --preset wasm`으로 빌드한 `hdTransEngine.js`(ES6 모듈, `MODULARIZE`)와
`hdTransEngine.wasm`입니다. C API를 그대로 내보내며, 사전 파일은 Emscripten 파일 시스템(`FS`)에 써서
`hdTrans_Initialize`에 그 폴더를 줍니다. TSV를 컴파일하는 동안의 메모리가 WebAssembly 메모리에 그대로 남으므로,
열 사전은 네이티브 CLI로 [컴파일한 사전](dictionaries.md#컴파일한-사전-hdsnap)으로 주는 편이 좋습니다:
`hdTrans_Initialize("/dicts.hdsnap\0/dicts\0")`(나머지 파일은 `/dicts`에).

- 내보내는 함수: `hdTrans_Initialize`, `hdTrans_Terminate`, `hdTrans_Translate`, `hdTrans_Reset`,
  `hdTrans_Reload`, `hdTrans_UserDictError`, `hdTrans_FreeMem`, `hdTrans_Version`, `hdTrans_Load`, `malloc`, `free`
- 런타임 메서드: `FS`, `ccall`, `cwrap`, `UTF8ToString`, `stringToUTF8`, `lengthBytesUTF8`, `getValue`, `HEAPU8`
- 사용자 사전은 파일 시스템 루트의 `/userdict.tsv`입니다. 스레드가 없어 자동으로 다시 읽지 않으므로, 파일을
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
  `UserReload::Auto`는 번역을 시작할 때 `userFile`을 1초에 한 번까지 확인해, 바뀌면 백그라운드에서 다시
  불러와 바꿉니다. 엔진을 없애면 진행 중인 다시 읽기가 끝나기를 기다립니다. `DetachReload()` 뒤에는 기다리지
  않고, 다시 읽기는 스스로 끝납니다(라이브러리를 내릴 때).
- `hdTrans::Session(const Engine&, bool reference = false)`: 문맥을 기억하는 번역. 한 번에 한 스레드에서
  씁니다. `Translate(text)`는 `\n`으로 구분한 한 줄 이상을 번역하고(`\r`은 유지), 64 KiB가 넘는 줄은
  16 KiB 이하 조각으로 나눠 번역합니다. `Reset()`은 기억을 지우고 엔진의 현재 사전을 가져옵니다.
  `UserReload::Auto` 엔진의 세션은 번역을 시작할 때 엔진에 새 사전이 있으면 기억을 지우고 그 사전을 씁니다.
