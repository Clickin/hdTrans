# 빌드

hdTrans는 CMake 프리셋(`CMakePresets.json`)으로 빌드합니다. 외부 라이브러리는 필요 없습니다. Ehnd 필터의
정규식에 쓰는 PCRE2 10.49는 소스째 `third_party/pcre2`에 들어 있어(라이선스 `LICENCE.md`) 함께
빌드되며, 16비트 라이브러리만 만듭니다.

## 요구 사항

| 플랫폼 | 필요한 것 |
|---|---|
| macOS, Linux | CMake 3.25 이상, C++20 컴파일러(clang 또는 gcc), Ninja |
| Windows | Visual Studio 2026 (C++ 도구 포함), CMake 4.2 이상 |
| WebAssembly | Emscripten, Ninja |

## 프리셋

| 프리셋 | 생성기 | 출력 폴더 | 내용 |
|---|---|---|---|
| `mac` | Ninja | `build/mac` | macOS clang Release |
| `mac-debug` | Ninja | `build/mac-debug` | macOS Debug |
| `linux` | Ninja | `build/linux` | Linux Release |
| `windows-x64` | Visual Studio 18 2026 | `build/windows-x64/Release` | MSVC x64 |
| `windows-x86` | Visual Studio 18 2026 | `build/windows-x86/Release` | MSVC x86 (32비트 호스트용 DLL) |
| `windows-x64-ninja` | Ninja | `build/windows-x64-ninja` | MSVC x64, x64 개발자 환경 필요 |
| `windows-x86-ninja` | Ninja | `build/windows-x86-ninja` | MSVC x86, x86 개발자 환경 필요 |
| `wasm` | Ninja | `build/wasm` | Emscripten |

네이티브 프리셋은 모두 정적 라이브러리(`hdTransCore`), 공유 라이브러리(`hdTransEngine`), CLI
(`hdtrans`), 단위 테스트(`hdtrans_tests`)를 빌드합니다. `J2KEngine.dll`은 Windows에서만 만들어집니다.

| 산출물 | Windows | Linux | macOS |
|---|---|---|---|
| C API 라이브러리 | `hdTransEngine.dll` | `libhdTransEngine.so` | `libhdTransEngine.dylib` |
| 이지트랜스 호환 DLL | `J2KEngine.dll` | - | - |
| CLI | `hdtrans.exe` | `hdtrans` | `hdtrans` |

## macOS, Linux

```sh
cmake --preset mac          # 또는 linux
cmake --build --preset mac --parallel
ctest --preset mac
```

이것이 기본 개발 루프입니다. 다른 네이티브 플랫폼에서는 `mac` 대신 해당 프리셋 이름을 씁니다.

## Windows

Visual Studio 프리셋은 개발자 명령 프롬프트 없이 일반 터미널에서 동작합니다.

```powershell
cmake --preset windows-x64  # 또는 windows-x86
cmake --build --preset windows-x64 --parallel
ctest --preset windows-x64
```

Release 바이너리는 `build\windows-x64\Release`(또는 `build\windows-x86\Release`)에 있습니다.
이지트랜스를 쓰던 도구에 넣을 `J2KEngine.dll`은 대부분 32비트이므로 `windows-x86`으로 빌드합니다.

Ninja로 빌드하려면 아키텍처에 맞는 MSVC 개발자 명령 프롬프트나 `vcvarsall` 환경에서
`windows-x64-ninja` 또는 `windows-x86-ninja`를 씁니다. 바이너리는 `build\<프리셋>`에 바로 생깁니다.

### tools/build.ps1

```powershell
powershell -File tools\build.ps1 -arch all
```

설치된 Visual Studio(2022 또는 2026)를 찾아 그 안의 CMake로 `windows-<arch>` 프리셋을 구성·빌드하고
`ctest`를 실행한 뒤, 각 DLL의 SHA-256과 크기를 출력합니다.

| 인자 | 설명 |
|---|---|
| `-arch x86\|x64\|all` | 빌드할 아키텍처 (기본 `all`) |
| `-ref REF` | `-bare`가 있을 때 체크아웃할 ref (기본 `main`) |
| `-root DIR` | 작업 트리 (기본 `HDTRANS_CPP_ROOT`, 없으면 스크립트가 있는 저장소) |
| `-bare DIR` | 가져올 bare 저장소 (기본 `HDTRANS_CPP_BARE`, 없으면 가져오지 않음). 주면 작업 트리를 `origin/<ref>`로 강제로 맞춥니다 |

성공하면 빌드한 커밋을 `build\SOURCE`에 적습니다.

## WebAssembly

```sh
emcmake cmake --preset wasm
cmake --build --preset wasm --parallel
```

`build/wasm/hdTransEngine.js`와 `hdTransEngine.wasm`이 생깁니다(ES6 모듈). 사용법은
[api.md](api.md#webassembly)에, 이것을 쓰는 웹 데모는 [web.md](web.md)에 있습니다.

## 빌드 옵션

| 옵션 | 기본값 | 설명 |
|---|---|---|
| `HDTRANS_WERROR` | `ON` | 경고를 오류로 처리합니다 (MSVC `/WX`, clang·gcc `-Werror`). `third_party/`의 코드는 경고 없이 빌드합니다 |
| `HDTRANS_PCRE2_JIT` | `OFF` | PCRE2 JIT 컴파일러를 넣습니다. WebAssembly에서는 쓰지 않습니다 |

`cmake --preset linux -DHDTRANS_WERROR=OFF`처럼 끌 수 있지만, 저장소의 코드는 경고 없이 빌드되어야
합니다. 경고는 플래그가 아니라 코드를 고쳐서 없앱니다.

컴파일 플래그: C++20, 라이브러리와 DLL은 예외와 RTTI 없이 빌드합니다. MSVC 빌드는 CRT를 정적으로
링크하므로 각 DLL은 단일 파일로 배포됩니다.

## CI

`.github/workflows/`의 두 워크플로가 GitHub Actions에서 돌아갑니다.

- **build.yml**: `linux`, `windows-x64`, `windows-x86`, `mac`, `wasm` 프리셋을 각 OS 러너에서
  빌드합니다. 모든 플랫폼에서 Ninja를 쓰고, 컴파일 캐시는 네이티브 빌드가 sccache, `wasm`이 ccache입니다
  (sccache는 Emscripten의 `-Xclang` 인자를 캐시하지 못합니다). Windows는 프리셋에 `-G Ninja`를 덧붙이고
  `vcvarsall`로 MSVC 환경을 설정합니다. 네이티브 프리셋은 `HDTRANS_DICTS=dicts`로 `ctest`를 실행하고, `wasm`은
  Node로 `tools/wasm_smoke.mjs`를 실행해 작은 사전으로 한 번 번역해 봅니다. 결과물은
  `hdtrans-<프리셋>` 아티팩트로 올라갑니다.
- **e2e.yml**: 풀 리퀘스트와 `main` 푸시 때, 기준 커밋과 현재 커밋의 CLI로 소설가가 되자 챕터를 번역해
  달라진 줄을 보고합니다. 자세한 내용은 [testing.md](testing.md#엔드투엔드-테스트-syosetu)에 있습니다.

로컬 빌드에는 sccache와 ccache 모두 필요 없습니다.
