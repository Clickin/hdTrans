# hdTrans 개발 문서

번역기를 쓰는 방법은 저장소 첫 화면의 [README](../README.md)에 있습니다. 이 폴더는 hdTrans를 빌드하고,
고치고, 다른 프로그램에 넣는 사람을 위한 문서입니다. 코드 작성 규칙은 [AGENTS.md](../AGENTS.md)를 따릅니다.

| 문서 | 내용 |
|---|---|
| [contributing.md](contributing.md) | 오역 하나를 고치는 순서: `-why` 진단, 고칠 곳 고르기, 테스트, 생성 파일, 판정, 커밋, 실제 사례 |
| [build.md](build.md) | 요구 사항, CMake 프리셋, Windows·WebAssembly 빌드, 빌드 옵션, CI |
| [testing.md](testing.md) | 단위 테스트, 예제 문서, 단계 기록, 자체 기준선, 엔드투엔드 테스트, 변경 판정(e2e + 모델 판정), 환경 변수 |
| [dictionaries.md](dictionaries.md) | `dicts/` 구성, 파일 머리줄과 계층, 열 파일, 사용자 사전 형식, 캐시 |
| [architecture.md](architecture.md) | 번역 파이프라인의 실행 층과 순서, 층마다 읽는 파일, `-why`가 적는 층과 규칙 |
| [rules.md](rules.md) | 단어 선택 규칙(`src/core/rules/*.txt`)의 주제 파일, 형식, 조건과 연산자, 고치는 방법, 규칙 테스트 |
| [codes.md](codes.md) | 품사·의미·문법 코드 일람(생성 문서): 뜻, 사전 행 수, 규칙 수, 예 |
| [rule-compiler.md](rule-compiler.md) | 규칙 컴파일러 `hdrulec`: 검사(lint), 읽기 쉬운 문법과 변환, C++ 코드 생성, 검증 |
| [ehnd-compat.md](ehnd-compat.md) | Ehnd 필터·SkipLayer·사용자 사전 형식과 적용 순서, hdTrans 필터 파일 |
| [api.md](api.md) | CLI 옵션, C API, J2K 함수, WebAssembly, C++ API |
| [web.md](web.md) | 웹 데모(`web/`) 빌드·확인·배포 |

## 저장소 구성

| 경로 | 내용 |
|---|---|
| `include/core/` | 공개 헤더: `engine.h`(C++), `hdtrans.h`(C) |
| `src/core/` | 엔진 라이브러리. 파이프라인 단계마다 폴더 하나 |
| `src/cli/` | `hdtrans` 명령 |
| `src/rulec/` | 규칙 컴파일러 `hdrulec`(빌드 호스트 도구) |
| `src/cshared/` | `hdTransEngine` C API |
| `src/j2kengine/` | `J2KEngine.dll`의 `J2K_*` 함수 |
| `src/wasm/` | WebAssembly 입구 |
| `web/` | 웹 데모 (https://trans.kuri.kr) |
| `dicts/` | 배포하는 기본 사전 |
| `tests/unit/` | 단위 테스트 (`hdtest.h` 러너) |
| `tests/host/` | C API·J2K DLL을 불러 쓰는 호스트 테스트 |
| `examples/` | 입력·출력 예제 문서 |
| `tools/` | `snapshot.sh`, `compare.sh`, `explain.sh`(자체 기준선), `examples.sh`(예제), `rule-cases.sh`(규칙 사례), `e2e.sh`, `fetch-syosetu.sh`(비공개 테스트 데이터), `detect-bad.sh`(잘못된 출력 찾기), `build.ps1`(Windows 빌드), `wasm_smoke.mjs`(WebAssembly 확인), `e2e-pairs.py`·`judge.py`·`attribute.py`(변경 판정), `code-glossary.py`(코드 일람), `rules-reorg/`(규칙 파일 분류표) |
