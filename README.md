# hdTrans

**이지트랜스와 호환되는 일한 기계번역 프로그램 hdTrans (하드도란스)**

\- by 소쿠릿 ([홈페이지](https://sokcuri.neko.kr/), [X](https://x.com/sokcuri))

하드도란스, 하드트랜스, 어떻게 읽어도 괜찮아요.

이지트랜스 쓰던 번역 도구에 DLL 하나만 바꿔 넣으면 그대로 돌아가요.
비주얼 노벨, 게임, 웹소설 번역에 맞춰 사전과 규칙을 계속 손보고 있어요.

- 설치할 것 없이 압축만 풀면 바로 써요.
- LunaTranslator, XUnity.AutoTranslator, 아랄트랜스, VNR, 아네모네에 그대로 끼워 쓸 수 있어요.
- 인명, 지명, 작품명, 관용구, 속어까지 사전에 들어 있어서 사용자 사전 없이도 꽤 잘 나와요.
- 앞에 나온 이름이나 문맥을 기억해서 다음 문장 번역에 써요.

## 먼저 써 보기

브라우저에서 바로 써 볼 수 있어요: https://trans.kuri.kr

## 내려받기

[Releases](https://github.com/sokcuri/hdTrans/releases)에서 받으면 돼요. Windows는 `hdTrans-0.1.0-windows.zip`처럼
버전과 `windows`가 붙은 파일이에요. 아무 폴더에나 풀면 끝이에요.

macOS, Linux 판은 명령줄 프로그램만 있어요([명령줄 사용법](docs/api.md#명령줄-hdtrans)).

## 쓰는 법

### 문장이나 파일 번역

`hdTransGUI.exe`를 켜서 위 칸에 일본어를 넣고 **번역하기**를 누르면 돼요.
텍스트 파일은 **문서 번역**에서 골라서 통째로 번역할 수 있어요.

### 이지트랜스 도구에 끼워 쓰기

1. 도구가 쓰는 이지트랜스 폴더에서 원래 `J2KEngine.dll`을 다른 이름으로 백업해 두세요.
2. 압축에서 나온 `J2KEngine.dll`과 `hdTrans` 폴더를 그 폴더에 복사하세요.
3. 도구를 다시 켜면 끝이에요.

아랄트랜스와 VNR은 이지트랜스가 깔려 있어야 폴더를 찾아요. 이지트랜스 없이 쓰는 법이나
도구별 설명은 [도구와 함께 쓰기](docs/using-with-tools.md)에 있어요.

## 단어 고치기 (사용자 사전)

`hdTrans\user\` 폴더에 `userdict.txt`를 만들고, 한 줄에 일본어와 한국어를 탭으로 띄워서 적으면 돼요.

```
魔導具	마도구
蒼月	아오츠키	surname
```

- 저장하면 프로그램을 켜 둔 채로도 다음 번역부터 바로 반영돼요.
- `userdict`로 시작하는 `.txt` 파일은 다 읽어요. 게임별로 파일을 나눠 둬도 돼요.
- 새 버전을 같은 폴더에 덮어 풀어도 `user` 폴더 안의 파일은 그대로 남아요.

품사나 자세한 형식은 [사용자 사전](docs/dictionary-files.md#사용자-사전)을 보세요.

## 사전

배포판의 기본 사전은 `hdTrans\dicts.hdsnap` 파일 하나예요. 버전은 옆의 `dicts.txt`에 적혀 있어요.
저장소의 `dicts/` 폴더(TSV)를 직접 쓰고 싶으면 `dicts.hdsnap`을 지우고 그 자리에 `dicts` 폴더를 넣으면 돼요([사전](docs/dictionary-files.md#배포판과-git의-사전)).

## 설정

`hdTrans\user\hdtrans.config.yaml`에 두면 돼요. 같은 폴더의 `hdtrans.config.example.yaml`을 복사해서 고치면 편해요.

꿀도르 같은 Ehnd 사전은 기본으로 꺼져 있어요. 설정에서 켤 수는 있는데, 켜면 번역이 오히려 나빠질 수 있어요.

## 오류, 오역 신고

[이슈](https://github.com/sokcuri/hdTrans/issues/new/choose)에 남겨 주세요.

## 기여

풀 리퀘스트는 `develop` 브랜치로 보내 주세요. 사전과 규칙 고치는 법은 [오역 고치기](docs/fixing-translations.md)에 있어요.

## 라이선스

[AGPL-3.0-or-later](LICENSE)

## 서드파티 라이선스

[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)

## 개발자용

빌드, 명령줄 옵션, 다른 프로그램에 넣어 쓰기, 사전과 규칙 고치기: [docs/](docs/README.md)
