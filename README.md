# 일한번역기 hdTrans

일본어를 한국어로 번역하는 무료 번역기입니다. 이지트랜스(ezTrans XP)를 바탕으로 다시 만들었고,
비주얼 노벨, 게임, 웹소설 번역에 맞춰 사전과 규칙을 계속 다듬고 있습니다.

- 설치 없이 압축만 풀면 됩니다.
- 이지트랜스를 쓰던 번역 도구(LunaTranslator, XUnity.AutoTranslator, 아랄트랜스, VNR, 아네모네)에 그대로 끼워 쓸 수 있습니다.
- 인명, 지명, 작품명, 관용구, 속어까지 들어 있어 사용자 사전 없이도 바로 쓸 수 있습니다.
- 앞에서 나온 이름과 문맥을 기억해 다음 문장 번역에 씁니다.

## 바로 써 보기

설치 없이 브라우저에서: https://trans.kuri.kr

## 내려받기

[Releases](https://github.com/sokcuri/hdTrans/releases)에서 받습니다. Windows는 `hdTrans-0.1.0-windows.zip`처럼
버전과 `windows`가 붙은 파일을 받으세요.
압축을 원하는 폴더에 풀면 끝입니다.

macOS, Linux 판에는 명령줄 프로그램만 있습니다([명령줄 사용법](docs/api.md#명령줄-hdtrans)).

## 쓰는 법

### 문장이나 파일 번역

`hdTransGUI.exe`를 실행해 위 칸에 일본어를 넣고 **번역하기**를 누릅니다.
텍스트 파일은 **문서 번역**에서 파일을 골라 번역합니다.

### 이지트랜스 도구에 끼워 쓰기

1. 도구가 쓰는 이지트랜스 폴더에서 원래 `J2KEngine.dll`을 다른 이름으로 백업합니다.
2. 압축에서 나온 `J2KEngine.dll`과 `hdTrans` 폴더를 그 폴더에 복사합니다.
3. 도구를 다시 켭니다.

아랄트랜스와 VNR은 이지트랜스가 설치되어 있어야 폴더를 찾습니다. 이지트랜스 없이 쓰는 방법과
도구별 자세한 설명은 [도구와 함께 쓰기](docs/using-with-tools.md)에 있습니다.

## 단어 고치기 (사용자 사전)

`hdTrans\user\` 폴더에 `userdict.txt`를 만들고, 한 줄에 일본어와 한국어를 탭으로 띄워 적습니다.

```
魔導具	마도구
蒼月	아오츠키	surname
```

- 저장하면 프로그램을 켜 둔 채로도 다음 번역부터 반영됩니다.
- `userdict`로 시작하는 `.txt` 파일은 모두 읽습니다. 게임별로 파일을 나눠도 됩니다.
- 새 버전을 같은 폴더에 덮어 풀어도 `user` 폴더의 파일은 그대로 남습니다.

품사와 더 자세한 형식은 [사용자 사전](docs/dictionary-files.md#사용자-사전)에 있습니다.

## 설정

`hdTrans\user\hdtrans.config.yaml`에 둡니다. 같은 폴더의 `hdtrans.config.example.yaml`을 복사해서 고치면 됩니다.

꿀도르 같은 Ehnd 사전은 기본으로 꺼져 있습니다. 설정에서 켤 수 있지만 번역 품질이 떨어질 수 있습니다.

## 오류, 오역 신고

[이슈](https://github.com/sokcuri/hdTrans/issues/new/choose)에 남겨 주세요.

## 기여

풀 리퀘스트는 `develop` 브랜치로 보내 주세요. 사전과 규칙을 고치는 방법은 [오역 고치기](docs/fixing-translations.md)에 있습니다.

## 라이선스

[AGPL-3.0-or-later](LICENSE)

## 서드파티 라이선스

| 라이브러리 | 라이선스 |
|---|---|
| [PCRE2](https://github.com/PCRE2Project/pcre2) 10.49 | [BSD-3-Clause (PCRE2 exception)](third_party/pcre2/LICENCE.md) |
| [SLJIT](https://github.com/zherczeg/sljit) (PCRE2에 포함) | [BSD-2-Clause](third_party/pcre2/deps/sljit/LICENSE) |
| [libyaml](https://github.com/yaml/libyaml) 0.2.5 | [MIT](third_party/libyaml/License) |

원문: [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)

## 개발자용

빌드, 명령줄 옵션, 다른 프로그램에 넣어 쓰기, 사전과 규칙 고치기: [docs/](docs/README.md)

---

개발 및 문의: 소쿠릿 (https://x.com/sokcuri)
