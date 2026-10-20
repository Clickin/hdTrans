# 웹 데모

https://trans.kuri.kr 의 웹 데모는 `web/`에 있습니다. 번역은 모두 브라우저 안에서 하며, 서버는 정적
파일(페이지, WebAssembly 엔진과 기본 사전 전체를 담은 `dicts.hdsnap`)만 내려 줍니다.

## 구성

| 파일 | 내용 |
|---|---|
| `web/index.html`, `web/style.css` | 페이지 |
| `web/src/app.ts` | 엔진과 사전을 받아(Cache Storage에 보관) 워커에 넘기고, 입력이 바뀌면 번역을 요청 |
| `web/src/worker.ts` | 워커. WASM과 사전 파일을 준비하고 `HdTrans` 인스턴스로 번역 |
| `web/src/translator.ts` | 공용 [TypeScript API](typescript-api.md). 문자열 번역, 사용자 사전과 Ehnd 설정, JSON 선택 기록 |
| `web/src/grid.ts` | 사용자 사전, 이름 표 편집기 (탭 구분 표, 붙여 넣기, 파일 열기, 저장) |
| `web/src/names.ts` | 이름 표를 사용자 사전 줄로 바꿈 (품사가 없으면 `name`) |
| `web/examples/`, `web/examples.tsv` | 예제 텍스트와 메뉴 이름 |
| `web/build.mjs` | 네이티브 CLI로 `dicts.hdsnap`을 컴파일하고 사이트를 `build/web/dist`에 만듦 |
| `web/check.mjs` | 헤드리스 Chrome으로 사이트를 확인 |
| `web/server.mjs`, `web/etag.mjs` | Node 정적 서버와 ETag 생성, 조건부 요청 비교 |
| `web/deploy.sh` | 서버에 올림, 되돌림 |

- 사용자 사전 표와 이름 표를 합쳐 `setUserDictRaw()`로 넣습니다. 잘못된 내용은 오류를 보여 주고 이전에 적용한 사전을 유지합니다.
- 문서 방식은 입력 전체를 한 세션으로(`hdtrans`와 같음), 줄 단위는 줄마다 새 세션으로(`hdtrans -each`와
  같음) 번역합니다. 번역 전에 `reset()`으로 세션을 비웁니다.
- 페이지 아래 버전 줄은 빌드한 커밋의 날짜(Asia/Seoul, `yyyy-MM-dd HH:mm:ss`)와 짧은 해시입니다.
- WASM과 사전 캐시도 manifest의 크기와 SHA-256을 검사합니다. 맞지 않는 항목은 지우고 다시 받아
  검사하며, 다운로드 실패 시 손상된 캐시를 쓰지 않습니다.
- 검증된 캐시에 ETag가 있으면 `If-None-Match`로 서버에 재검증합니다. `304`는 검증된 캐시를 쓰고,
  `200`은 응답의 크기와 SHA-256을 다시 검사한 뒤 새 ETag와 함께 저장합니다. ETag는 무결성 검사를
  대체하지 않습니다. 네트워크 연결 실패 시에는 검증된 정상 캐시만 사용합니다. ETag가 없는 기존
  캐시나 서버에서는 크기와 SHA-256 검사를 유지합니다.

## 빌드

Node 22 이상과 Emscripten, 그리고 같은 소스로 빌드한 네이티브 CLI가 필요합니다. CLI가
`hdtrans -dicts dicts -compile-snapshot build/web/dicts.hdsnap`으로 기본 사전 전체를 컴파일하고, build.mjs는 그 파일을
WebAssembly 엔진이 불러오는지 확인한 뒤 올립니다. 엔진 데이터(`src/core/`)가 다른 빌드끼리는 맞지 않습니다.

```sh
cmake --preset mac && cmake --build --preset mac --parallel   # 또는 linux
emcmake cmake --preset wasm
cmake --build --preset wasm --parallel
npm --prefix web ci
node web/build.mjs
```

| 환경 변수 | 설명 |
|---|---|
| `HDTRANS_DICTS` | 올릴 사전 폴더 (기본 `dicts`) |
| `HDTRANS_WASM` | 엔진 빌드 폴더 (기본 `build/wasm`) |
| `HDTRANS_CLI` | 사전을 컴파일할 CLI (기본 `build/mac/hdtrans`, 없으면 `build/linux/hdtrans`) |
| `HDTRANS_WEB_EXAMPLES` | 저장소에 없는 예제 텍스트 폴더. `examples.tsv`에 적힌 파일 중 `web/examples/`에 없는 것을 여기서 찾음. 예제가 하나라도 없으면 빌드 실패 |

`build/web/dist`에는 `index.html`, `js/`, `examples/`, `manifest.json`과 내용 해시 폴더
`assets/<sha256 앞 16자>/`(`hdtrans_engine.js`, `hdtrans_engine.wasm`, `dicts.hdsnap`)가 생기고, 텍스트, wasm, `.hdsnap`
파일마다 `.br`, `.gz` 압축본이 붙습니다. 압축본을 제공하려면 배포 서버에서 인코딩별 응답과 ETag를
설정합니다. 아래 Node 서버는 압축하지 않은 원본만 제공합니다.

## 정적 서버

Node 표준 라이브러리만 사용합니다. 기본 디렉터리는 `build/web/dist`, 주소는 `127.0.0.1:8000`입니다.

```sh
node web/server.mjs                          # 또는 pnpm --dir web serve
node web/server.mjs build/web/dist 9000       # 디렉터리와 포트 지정
node web/server_test.mjs                     # 또는 pnpm --dir web test:server
```

`strongEtag(bytes)`는 응답 바이트의 SHA-256으로 strong ETag를 만듭니다.
`matchesIfNoneMatch(header, etag)`는 GET과 HEAD의 weak 비교, 태그 목록과 `*`를 처리합니다.
태그가 일치하면 본문 없는 `304`, 다르면 ETag와 본문을 가진 `200`을 반환합니다. HEAD는 본문 없이
같은 메타데이터를 반환합니다. 파일 크기와 수정 시간이 같아도 내용이 다르면 ETag가 달라집니다.

요청마다 파일을 읽어 같은 버퍼를 해시하고 전송하므로 파일 크기만큼 메모리가 필요합니다.
`Cache-Control: no-cache`로 재검증하며 TLS와 압축은 제공하지 않습니다. 외부 공개 시에는
역방향 프록시 뒤에 두고, 문서 루트는 신뢰할 수 있는 정적 파일로만 구성합니다.
경로 순회와 루트 밖 심볼릭 링크는 거부하지만, 비신뢰 프로세스가 동시에 파일 경로를 바꾸는
환경을 격리하는 서버는 아닙니다.

## 확인

```sh
node web/server.mjs build/web/dist 8000 &
head -20 INPUT > in.txt
build/mac/hdtrans -dicts dicts -userdict '' -in in.txt > want.txt
node web/check.mjs http://127.0.0.1:8000/ INPUT want.txt 20
```

`check.mjs`는 두 번 방문해 준비 시간, 받은 바이트, 엔진 메모리를 적고, 입력 앞 20줄의 번역이 CLI 출력과
같은지, 긴 입력의 진행 막대, 사용자 사전, 이름 표 편집과 붙여 넣기가 번역을 바꾸는지, 버전 줄을
확인하고, 번역 API가 없는지(`POST /api/translate`가 404) 봅니다.
`CHROME`으로 Chrome 경로를, `SHOT`으로 화면 저장 경로를 줍니다.

캐시 무결성은 실제 Cache Storage의 내용과 길이를 바꾸고, 다운로드를 차단해 확인합니다.

```sh
BOUNDARY=cache node web/check.mjs http://127.0.0.1:8000/
BOUNDARY=etag node web/check.mjs http://127.0.0.1:8000/   # ETag를 제공하는 서버 필요
```

## 배포

```sh
HDTRANS_WEB_HOST=user@host HDTRANS_WEB_SSH='ssh -i KEY' web/deploy.sh
```

올리기 전에 서버의 사이트 폴더를 `<폴더>.bak-<UTC 시각>`으로 복사하고, 되돌리는 명령을 출력합니다.

```sh
HDTRANS_WEB_HOST=user@host HDTRANS_WEB_SSH='ssh -i KEY' web/deploy.sh rollback <폴더>.bak-<UTC 시각>
```

`HDTRANS_WEB_DIR`로 서버의 사이트 폴더를 바꿉니다(기본 `/srv/hdtrans/web`). 접속 정보와 키는 저장소에
두지 않습니다. 올린 뒤 `node web/check.mjs https://trans.kuri.kr/ ...`로 확인합니다.

## 메모리

WebAssembly의 선형 메모리는 해제해도 크기가 줄지 않으며, 빈 공간을 이후 호출에서 재사용합니다.
페이지는 TSV를 직접 컴파일하지 않고 `dicts.hdsnap`을 읽어 초기화 중 메모리 사용을 줄입니다.
사용자 사전과 Ehnd 필터는 같은 인스턴스에서 교체하며, 번역 결과의 임시 메모리는 자동으로 정리합니다.
