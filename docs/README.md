# hdTrans 개발 문서

번역기를 쓰는 방법은 저장소 첫 화면의 [README](../README.md)에 있습니다. 이 폴더는 hdTrans를 빌드하고,
고치고, 다른 프로그램에 넣는 사람을 위한 문서입니다. 코드 작성 규칙은 [AGENTS.md](../AGENTS.md)를 따릅니다.

| 문서 | 내용 |
|---|---|
| [build.md](build.md) | 요구 사항, CMake 프리셋, Windows·WebAssembly 빌드, 빌드 옵션, CI |
| [testing.md](testing.md) | 단위 테스트, 예제 문서, 단계 기록, 자체 기준선, 엔드투엔드 테스트, 환경 변수 |
| [dictionaries.md](dictionaries.md) | `dicts/` 구성, 파일 머리줄과 계층, 열 파일, 사용자 사전 형식, 캐시 |
| [api.md](api.md) | CLI 옵션, C API, J2K 함수, WebAssembly, C++ API |
| [web.md](web.md) | 웹 데모(`web/`) 빌드·확인·배포 |

## 저장소 구성

| 경로 | 내용 |
|---|---|
| `include/core/` | 공개 헤더: `engine.h`(C++), `hdtrans.h`(C) |
| `src/core/` | 엔진 라이브러리. 파이프라인 단계마다 폴더 하나 |
| `src/cli/` | `hdtrans` 명령 |
| `src/cshared/` | `hdTransEngine` C API |
| `src/j2kengine/` | `J2KEngine.dll`의 `J2K_*` 함수 |
| `src/wasm/` | WebAssembly 입구 |
| `web/` | 웹 데모 (https://trans.kuri.kr) |
| `dicts/` | 배포하는 기본 사전 |
| `tests/unit/` | 단위 테스트 (`hdtest.h` 러너) |
| `tests/host/` | C API·J2K DLL을 불러 쓰는 호스트 테스트 |
| `tests/e2e/` | 엔드투엔드 테스트가 쓰는 웹소설 챕터를 가리키는 한 줄짜리 스텁 |
| `examples/` | 입력·출력 예제 문서 |
| `tools/` | `snapshot.sh`, `compare.sh`, `explain.sh`(자체 기준선), `examples.sh`(예제), `fetch-syosetu.sh`, `e2e.sh`(엔드투엔드), `detect-bad.sh`(잘못된 출력 찾기), `build.ps1`(Windows 빌드), `wasm_smoke.mjs`(WebAssembly 확인) |
