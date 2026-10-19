# 일한번역기 hdTrans

hdTrans는 일본어를 한국어로 옮기는 규칙 기반 번역 엔진입니다. 이지트랜스(ezTrans XP) 엔진에서
출발해 코어를 유니코드로 다시 만들었고, 누구나 읽고 고칠 수 있도록 소스를 공개합니다. 일반 어휘에
더해 인명, 지명, 작품명, 관용구와 속어까지 기본 사전에 들어 있어 사용자 사전 없이도 비주얼 노벨,
에로게, 웹소설을 번역할 수 있습니다.

## 특징

- **유니코드 코어**: 내부 처리는 모두 UTF-8입니다. Shift-JIS와 CP949는 이지트랜스 호환 함수의
  입출력에서만 쓰입니다.
- **문맥을 기억하는 번역**: 한 문서(세션) 안에서 앞서 나온 이름과 문맥이 뒤 문장의 번역에 반영됩니다.
- **풍부한 기본 사전**: 일반 어휘, 인명, 캐릭터, 지명, 단체, 작품명, 관용구, 속어, 이표기 사전을 함께 제공합니다.
- **이지트랜스 호환**: `J2KEngine.dll`을 바꿔 넣으면 이지트랜스를 쓰던 후킹, 번역 도구를 그대로 쓸 수 있습니다.
- **간단한 사용자 사전**: 한 줄에 `일본어<TAB>한국어` 한 항목. 동사, 형용사는 기본형만 적으면 활용됩니다.
- **가볍고 빠름**: 함께 넣은 PCRE2 말고는 외부 의존성이 없고, 여러 스레드에서 동시에 번역할 수 있으며, 컴파일한 사전
  캐시로 빠르게 시작합니다.

## 웹 데모

설치 없이 브라우저에서 써 볼 수 있습니다: https://trans.kuri.kr

## 설치

미리 빌드한 파일은 [GitHub Releases](https://github.com/sokcuri/hdTrans/releases)로 배포할
예정이며 아직 준비 중입니다. 지금은 [docs/build.md](docs/build.md)를 따라 소스에서 빌드해 주세요.

번역에는 사전이 필요합니다. 저장소의 [`dicts/`](dicts/) 폴더가 기본 사전이며, 빌드한 프로그램과 함께
이 폴더를 사용합니다.

## 사용법

### 명령줄: `hdtrans`

표준 입력(또는 `-in 파일`)의 UTF-8 텍스트를 번역해 표준 출력으로 씁니다. 입력 전체가 하나의
문서로 번역됩니다.

```sh
printf '%s\n' '今日はいい天気ですね。' | hdtrans -dicts dicts
hdtrans -dicts dicts -in script.ja.txt > script.ko.txt
```

| 옵션 | 설명 |
|---|---|
| `-dicts DIR[,DIR...]` | 사전 폴더. 쉼표로 여러 개를 주면 뒤 폴더가 앞 폴더를 덮어씁니다 (기본값: 실행 파일 옆 `dicts`, 없으면 현재 폴더의 `dicts`) |
| `-userdict FILE` | 사용자 사전 파일 (기본값: 실행 파일 옆 `userdict.tsv`, 빈 값이면 쓰지 않음) |
| `-in FILE` | 표준 입력 대신 파일에서 읽기 |
| `-each` | 줄마다 새 세션으로 번역 (줄끼리 문맥을 나누지 않음) |
| `-compile-dicts` | 사전 캐시를 미리 만들고 종료 |

전체 옵션과 환경 변수는 [docs/api.md](docs/api.md#명령줄-hdtrans)에 있습니다.

### 이지트랜스 대체: `J2KEngine.dll`

`J2KEngine.dll`은 이지트랜스 엔진과 같은 `J2K_*` 함수를 내보냅니다. 이지트랜스 엔진을 불러오는
기존 후킹, 번역 도구에서 원래 `J2KEngine.dll` 대신 이 파일을 쓰면 됩니다. 이런 도구는 대부분
32비트이므로 **x86(32비트) 빌드**를 사용하세요.

1. 이지트랜스 폴더의 원래 `J2KEngine.dll`을 백업하고 hdTrans의 `J2KEngine.dll`로 바꿉니다.
2. 같은 폴더에 `dicts\`를 만들고 저장소 `dicts/`의 `*.tsv` 파일을 복사합니다.
3. 사용자 사전을 쓰려면 같은 폴더에 `userdict.tsv`를 둡니다. 도구가 실행 중일 때 고쳐 저장해도
   다음 번역부터 자동으로 반영됩니다. 도구의 사용자 사전 다시 읽기(`J2K_ReloadUserDict`)는 바로
   반영합니다.

도구가 넘기는 사전 경로는 무시되고, 사전은 항상 DLL 옆의 `dicts\`와 `userdict.tsv`에서 찾습니다.

### C API: `hdTransEngine.dll`

직접 프로그램에 넣을 때는 C API를 씁니다. 문자열은 모두 UTF-8입니다
(Linux `libhdTransEngine.so`, macOS `libhdTransEngine.dylib`).

```c
#include "core/hdtrans.h"

hdTrans_Initialize("dicts\0");    /* 사전 폴더 목록: NUL로 구분, 끝은 NUL 두 개. 라이브러리 옆 userdict.tsv도 읽음 */
char* out; size_t n;
if (hdTrans_Translate(text, len, &out, &n) == 0) {
    /* out: 번역 결과 (UTF-8) */
    hdTrans_FreeMem(out);
}
hdTrans_Terminate();
```

함수별 설명과 J2K 함수 목록, WebAssembly 사용법은 [docs/api.md](docs/api.md)에 있습니다.

### 사용자 사전

프로그램(`hdtrans`, `J2KEngine.dll`, `hdTransEngine.dll`) 옆에 `userdict.tsv`를 만들고 한 줄에 한
항목씩 탭으로 구분해 적습니다. `#`이나 `//`로 시작하는 줄은 주석입니다.

```
# 일본어	한국어	품사
魔導具	마도구
蒼月	아오츠키	surname
走り回る	뛰어다니다	v5
```

품사를 생략하면 명사입니다. 두 DLL은 파일이 바뀌면 알아서 다시 읽으므로, 번역 도구를 켜 둔 채
편집기로 고쳐 저장하면 됩니다. 잘못된 줄이 있으면 사용자 사전만 빼고 번역하며, 고쳐 저장하면 다시
반영됩니다. CLI는 시작할 때 읽으며(문제가 있으면 경고), 다른 파일은 `-userdict FILE`로 줍니다.
품사 목록과 옵션은 [docs/dictionary-files.md](docs/dictionary-files.md#사용자-사전)를 참고하세요.

Ehnd(꿀도르 등)의 필터와 사용자 사전은 `Ehnd` 폴더를 `hdtrans`나 `J2KEngine.dll` 옆에 두면 그대로
씁니다(다른 폴더는 `hdtrans -ehnd DIR`). 형식과 차이는 [docs/ehnd-compat.md](docs/ehnd-compat.md)에 있습니다.

## 지원 플랫폼

- Windows x86 / x64 (`hdtrans.exe`, `hdTransEngine.dll`, `J2KEngine.dll`)
- Linux
- macOS
- WebAssembly

## 개발 문서

빌드, 테스트, 사전 구조, API: [docs/](docs/README.md)

---

개발 및 문의사항 : 소쿠릿 (https://x.com/sokcuri)
