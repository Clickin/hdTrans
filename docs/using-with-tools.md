# 도구와 함께 쓰기

## 공통 준비

Releases 배포는 아직 준비 중입니다. 지금은 [Build의 성공한 실행](https://github.com/sokcuri/hdTrans/actions/workflows/build.yml?query=branch%3Aci%2Ftool-compat)에서
**`hdtrans-windows-x86` artifact zip**을 받거나 [직접 빌드](build.md)합니다. J2K 연결에는 x64가 아닌 x86 파일을 씁니다.
현재 artifact에는 사전이 없으므로 저장소의 `dicts/`도 따로 복사해야 합니다.

엔진 폴더를 하나 정합니다. 기존 이지트랜스 폴더를 쓰면 원래 DLL부터 백업하세요.

```text
C:\hdTrans\
  J2KEngine.dll
  dicts\             ← 저장소 dicts/ 전체
  userdict.tsv       ← 선택, 사용자 사전
```

`hdtrans.config.yaml` 설정 파일도 DLL 바로 옆에 두는 방식이 다른 브랜치에서 추가 중이며, 이 브랜치에는 아직 적용되지 않습니다.

hdTrans는 초기화 인자의 `Dat` 경로, 현재 폴더, 레지스트리와 무관하게 **DLL 옆**의 사전을 읽습니다.
아래에서 확인한 로더는 `Dat` 폴더나 이지트랜스 실행 파일의 존재를 필수 검사하지 않으므로 빈 `Dat`나 원본 엔진을 넣을 필요가 없습니다.
경로는 도구의 ANSI 변환 문제를 피하도록 우선 `C:\hdTrans` 같은 영문 경로를 쓰세요.

**기존 Ehnd DLL 위에 hdTrans를 겹쳐 넣는 방식은 검증하지 않았습니다.** 원본 Ehnd는 `j2kengine.dlx`의 내부 코드를
패치하므로 export 호환만으로 그 조합을 보장할 수 없습니다([소스][ehnd]). hdTrans 자체가 Unicode API를 제공하며,
Ehnd 데이터는 DLL 옆 `Ehnd/`에서 읽습니다. 지원 범위는 [Ehnd 호환 안내](ehnd-compat.md)를 보세요.

## 도구별 연결

아래의 **CI 검증은 도구 소스의 DLL 호출을 별도 호스트로 재현한 것**입니다. 실제 프로그램을 실행한 수동 검증은 아닙니다.
경로 선택 UI, IPC, 후킹, 클립보드, 도구 자체 전후처리는 검증에 포함하지 않습니다.

### LunaTranslator

- 이지트랜스 번역 설정에서 공통 준비의 `J2KEngine.dll` 파일을 선택합니다. 설치 폴더 선택이 아닙니다.
- [Python 경로 처리][luna-python]는 선택 파일의 부모를 **32비트 `LunaSubProcess32.exe`**에 넘깁니다. 메인 앱은 64비트여도 됩니다.
- [네이티브 호출][luna-native]: DLL 로드 → 필수 export 19개 검사 → `InitializeEx("CSUSER123455", "<폴더>\\Dat")`
  → `TranslateMMNTW(0, text)` → 복사 → `FreeMem(pointer)`. `Dat`는 시스템 ANSI, 번역은 UTF-16입니다.
  W export 존재를 Ehnd 지원으로 판단하며, 별도 Ehnd DLL을 찾지는 않습니다. 요청은 직렬 처리하고 Terminate와 unload는 하지 않습니다.
- **CI:** `host.luna`, `host.luna-ansi`. hdTrans에서는 W 경로를 선택합니다. ANSI 테스트는 W가 없다는 조건을 강제로 선택하여
  CP932 입력, CP949 출력의 fallback도 확인합니다. 실프로그램에서는 초기화 오류 표시와 연속 번역을 확인해야 합니다.

### XUnity.AutoTranslator의 ezTransXP

- 공통 준비 후 `Config.ini`에 `[Service] Endpoint=ezTransXP`, `[ezTrans] InstallationPath=C:\hdTrans`를 설정합니다.
  기존 `J2KEngineH.dll`이 있으면 백업 후 다른 이름으로 옮기세요. 이 도구는 **H 파일을 우선** 읽습니다.
- [설정 소스][xunity-endpoint]: InstallationPath 기본값은 `HKCU\SOFTWARE\ChangShin\ezTrans`의 `FilePath`입니다.
  경로를 명시하면 레지스트리를 만들 필요가 없습니다. 별도 `FullNET\ezTransXP.ExtProtocol.exe`가 엔진을 부릅니다.
- [호출 소스][xunity-native]: Stop, TranslateW, InitializeEx 주소 조회 → 같은 키와 `Dat`로 초기화
  → `TranslateMMNTW(0, UTF-16)` → `PtrToStringAuto`. 직렬 처리하며 종료는 **인자 없는 `StopTranslation()` → FreeLibrary**, Terminate는 없습니다.
- **CI:** `host.xunity`는 원래의 **Cdecl C# delegate**를 사용합니다. DLL의 stdcall과 선언이 달라 원본 net35 helper의
  스택 복구 동작을 .NET Framework 4의 `NetFx40_PInvokeStackResilience` 설정으로 재현합니다. 최신 .NET이나 Mono까지 검증한 것은 아닙니다.
- **제한:** 원본 호출자는 `FreeMem`을 전혀 부르지 않아 **매 번역마다 결과 버퍼가 누수**됩니다. 원본 Ehnd도 매번 새 버퍼를
  할당합니다([소스][ehnd]). CI 통과를 무누수 보장으로 해석하면 안 됩니다. 실제 배포 helper의 비트수, CLR, 게임 연결은 수동 확인이 필요합니다.

### 아네모네, Anemone

- 공통 준비 후 아네모네를 끄고, 실행 파일 옆 `anemone.ini`를 편집합니다.

  ```ini
  [CONFIG]
  EZTRANS_PATH=C:\hdTrans
  ```

- 값은 DLL 파일이 아닌 폴더입니다. [경로 탐색][anemone-path]은 이 설정의 `J2KEngine.dll`을 열어 보고,
  실패하면 `HKCU\Software\ChangShin\ezTrans`의 `FilePath`를 읽습니다.
- [엔진 로더][anemone-native]와 [번역 호출][anemone-text]은 Luna와 같은 19개 필수 export, 같은 키와 `Dat`,
  W 우선 및 CP932/CP949 fallback, 결과 복사 후 `FreeMem`입니다. 번역과 reload는 공통 잠금으로 직렬화하며 Terminate와 unload는 하지 않습니다.
  Win32 전용입니다. `Dat` 경로는 문자열을 바이트로 단순 축소하므로 비ASCII 경로를 보장하지 않습니다.
- **CI:** `host.anemone`, `host.anemone-ansi`. 실제 W 경로에서는 `AneDic.txt`를 `Dat\UserDict.jk`에 적용하는 구형 기능을 쓰지 않습니다.
  사용자 사전은 공통 준비의 `userdict.tsv`로 둡니다. 클립보드 감시, 창 출력과 실제 사용자 사전 편집은 수동 확인이 필요합니다.

### Textractor + 아네모네

- Textractor의 **Copy to Clipboard** 확장을 켜고, 위와 같이 설정한 아네모네에서 클립보드 번역을 켭니다.
- [공식 확장 목록][textractor-extensions]에는 ezTrans 로더가 없습니다. [클립보드 확장][textractor-clipboard]이
  UTF-16 `CF_UNICODETEXT`를 넘기고 **아네모네가 DLL을 호출**하는 구성입니다. Textractor와 아네모네의 비트수는 같지 않아도 됩니다.
- **CI:** DLL 부분은 `host.anemone`과 같습니다. Textractor 자체의 직접 로드 검증이 아닙니다.
  게임 후킹, 선택한 텍스트 스레드, 클립보드 전달과 중복 번역은 실제 Windows에서 확인해야 합니다.

### HTTP 중개: nanikit/eztrans-server

- 공통 준비 후 terminal 버전에서 `EztransServer.exe --dll-path "C:\hdTrans\J2KEngine.dll"`로 지정합니다.
  x86 .NET 6 런타임이 필요합니다. GUI 버전은 명시 경로 설정 없이 자동 탐색합니다.
- [서버][server]가 참조하는 Nanikit.Ehnd 2.4.0의 [로더][server-interop]는 명시 경로 → 32비트 HKCU `FilePath`
  → 기본 이지트랜스 폴더 → 서버 실행 폴더와 상위 폴더 순으로 탐색합니다.
- [호출][server-translate]: 기본 Winapi 규약으로 InitializeEx 조회와 초기화 → FreeMem, TranslateW 조회
  → `TranslateMMNTW(0, UTF-16)` → 복사 → `FreeMem`. 키와 `Dat` 인자는 위와 같습니다. 서버 큐와 semaphore로 직렬 처리하고 Terminate와 unload는 하지 않습니다.
- **CI:** `host.eztransserver`. 2.4.0을 표시한 소스의 네이티브 호출을 재현합니다. NuGet 바이너리 동일성, HTTP 요청과 응답,
  GUI 자동 탐색은 수동 검증 대상입니다. 기본 서버 주소는 `http://localhost:8000/`입니다.

### 그 밖의 도구

| 도구 | 확인한 범위와 제한 |
|---|---|
| [HelloKS/ezTransWeb][eztransweb] | 소스 조사만 함, CI 없음. x86 Python의 WinDLL로 **J2KEngineH.dll**과 `Dat` 경로를 하드코딩하고 같은 키, W 번역을 사용합니다. FreeMem 없이 Flask 스레드들이 잠금 없이 부릅니다. 파일명만 바꿔 호환 확인 완료로 볼 수 없습니다. |
| [Translator++ transEz][translatorez] | 공식 ezTransXP 지원 공지는 있으나 플러그인 구현 소스를 확보하지 못했습니다. DLL 이름, 호출, 설치 경로와 hdTrans 호환성은 미검증이므로 교체 절차를 제시하지 않습니다. |
| [MTool][mtool] | 공개 ezTrans 연동 구현과 지원 여부를 확인하지 못했습니다. hdTrans 연결 절차와 CI 검증은 없습니다. |

## CLI로 텍스트 파일 번역

플랫폼에 맞는 `hdtrans`와 `dicts/`를 함께 둡니다. 입력과 출력은 UTF-8입니다.

```sh
hdtrans -in script.ja.txt > script.ko.txt
hdtrans -each -in lines.ja.txt > lines.ko.txt
```

기본은 파일 전체가 한 문서입니다. `-each`는 줄마다 문맥을 새로 시작합니다. [전체 옵션](api.md#명령줄-hdtrans).

## C API와 Python ctypes

직접 연결할 때는 `J2KEngine.dll`이 아닌 **`hdTransEngine.dll`**을 씁니다. Python과 같은 비트수의 Windows zip을 고릅니다.
macOS는 `libhdTransEngine.dylib`, Linux는 `libhdTransEngine.so`이며 문자열은 모두 UTF-8, 호출 규약은 Cdecl입니다.

아래를 `translate.py`로 저장하고 `python translate.py C:\hdTrans\hdTransEngine.dll C:\hdTrans\dicts`로 실행합니다.
macOS dylib로 실행하여 `안녕하세요` 출력을 확인한 예제입니다. Windows Python 자체의 실행은 아직 수동 확인 대상입니다.

```python
import ctypes as C
from pathlib import Path
import sys

api = C.CDLL(str(Path(sys.argv[1]).resolve()))
api.hdTrans_Initialize.argtypes = [C.c_char_p]
api.hdTrans_Initialize.restype = C.c_int
api.hdTrans_Translate.argtypes = [C.c_char_p, C.c_size_t,
                                 C.POINTER(C.c_void_p), C.POINTER(C.c_size_t)]
api.hdTrans_Translate.restype = C.c_int
api.hdTrans_FreeMem.argtypes = [C.c_void_p]
api.hdTrans_FreeMem.restype = None
api.hdTrans_Terminate.argtypes = []
api.hdTrans_Terminate.restype = None

dirs = str(Path(sys.argv[2]).resolve()).encode("utf-8") + b"\0\0"
if api.hdTrans_Initialize(dirs) != 0:
    raise RuntimeError("사전을 불러오지 못했습니다")
try:
    text = "こんにちは".encode("utf-8")
    out, size = C.c_void_p(), C.c_size_t()
    if api.hdTrans_Translate(text, len(text), C.byref(out), C.byref(size)) != 0:
        raise RuntimeError("번역 실패")
    try:
        print(C.string_at(out, size.value).decode("utf-8"))
    finally:
        api.hdTrans_FreeMem(out)
finally:
    api.hdTrans_Terminate()
```

반환형을 `c_char_p`로 바꾸면 원래 포인터를 잃으므로 `c_void_p`를 유지합니다. [C API 계약](api.md#c-api-hdtransengine).

## 웹과 WASM

설치 없이 쓰려면 **https://trans.kuri.kr** 에 텍스트를 넣습니다. 직접 브라우저 앱에 넣으려면 `hdtrans-wasm` artifact의
`hdTransEngine.js`, `hdTransEngine.wasm`과 사전 파일을 함께 배포합니다. 직접 빌드는
`emcmake cmake --preset wasm && cmake --build --preset wasm`입니다. [WASM API](api.md#webassembly)와 [웹 빌드](web.md)를 참고하세요.
Windows DLL을 WASM에 넣는 방식은 아닙니다.

## CI가 확인하는 것

구현은 [네이티브 호스트](../tests/host/tool_compat.cpp)와 [C# 호스트](../tests/host/xunity_host.cs)에 있습니다.
모든 모드는 없는 `Dat` 경로, 사전 없는 작업 폴더, 잘못된 `HDTRANS_DICTS`를 사용하고 DLL 옆 사전으로 초기화합니다.
`こんにちは → 안녕하세요`, `日本語 → 일본어`를 반복하여 정확한 인코딩과 결과를 비교하고, 도구의 해제 및 종료 순서를 그대로 따릅니다.
하네스로 재현한 도구는 DLL 번역을 직렬화하므로 여러 스레드의 동시 번역은 기존 `host.j2k`에서 별도로 검사합니다.
x86 CI가 실제 엔진 ABI 검증이며 x64 실행은 같은 인터페이스의 추가 검사일 뿐, x64 이지트랜스 배포 지원을 뜻하지 않습니다.

[luna-native]: https://github.com/HIllya51/LunaTranslator/blob/168272ad5088b4a235fd37e1c822d2cb5047c449/src/NativeImpl/LunaSubprocess/eztrans.cpp
[luna-python]: https://github.com/HIllya51/LunaTranslator/blob/168272ad5088b4a235fd37e1c822d2cb5047c449/src/LunaTranslator/translator/eztrans.py
[xunity-endpoint]: https://github.com/bbepis/XUnity.AutoTranslator/blob/08324a58dcb26b76fc08e0fb2121098164579e73/src/Translators/ezTransXP/ezTransXPEndpoint.cs
[xunity-native]: https://github.com/bbepis/XUnity.AutoTranslator/blob/08324a58dcb26b76fc08e0fb2121098164579e73/src/Translators/ezTransXP.ExtProtocol/ezTransTranslationLibrary.cs
[anemone-native]: https://github.com/sokcuri/anemone/blob/9fa75743af5b433253036676bcd962928f120cbf/Anemone/TransEngine.cpp
[anemone-text]: https://github.com/sokcuri/anemone/blob/9fa75743af5b433253036676bcd962928f120cbf/Anemone/TextProcess.cpp
[anemone-path]: https://github.com/sokcuri/anemone/blob/9fa75743af5b433253036676bcd962928f120cbf/Anemone/Library.cpp
[textractor-extensions]: https://github.com/Artikash/Textractor/blob/df5b830d1757a66aa162f8081e99d42c9bd50488/extensions/CMakeLists.txt
[textractor-clipboard]: https://github.com/Artikash/Textractor/blob/df5b830d1757a66aa162f8081e99d42c9bd50488/extensions/copyclipboard.cpp
[server]: https://github.com/nanikit/eztrans-server/blob/8b398d67fcdba6ab9a97fc0016488c65469e16f4/EztransServer.Core/EztransServer.Core.csproj
[server-interop]: https://github.com/nanikit/Nanikit.Ehnd/blob/7599abd6efe45e834f0dfee268201cefeea3a05a/Nanikit.Ehnd/EhndInterop.cs
[server-translate]: https://github.com/nanikit/Nanikit.Ehnd/blob/7599abd6efe45e834f0dfee268201cefeea3a05a/Nanikit.Ehnd/Ehnd.cs
[eztransweb]: https://github.com/HelloKS/ezTransWeb/blob/7296b9acd71e91eb28079af65dc9af83e73c028c/eztrans.py
[ehnd]: https://github.com/sokcuri/ehnd/tree/a2c6e0fbb309690b440a2e65027076b67e1cd3a2/ehnd
[translatorez]: https://dreamsavior.net/update-info-translator-ver-5-11-15/
[mtool]: https://trstest.mtool.app/release.php?lang=en
