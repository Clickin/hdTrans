# 웹 데모

https://trans.kuri.kr 의 웹 데모는 `web/`에 있습니다. 번역은 모두 브라우저 안에서 하며, 서버는 정적
파일(페이지, WebAssembly 엔진, `dicts/`의 사전 파일)만 내려 줍니다.

## 구성

| 파일 | 내용 |
|---|---|
| `web/index.html`, `web/style.css` | 페이지 |
| `web/src/app.ts` | 엔진과 사전을 받아(Cache Storage에 보관) 워커에 넘기고, 입력이 바뀌면 번역을 요청 |
| `web/src/worker.ts` | 워커. `hdTransEngine.js`를 불러 사전을 `/dicts`에 쓰고 C API로 번역 |
| `web/src/grid.ts` | 사용자 사전·이름 표 편집기 (탭 구분 표, 붙여 넣기·파일 열기·저장) |
| `web/src/names.ts` | 이름 표를 사용자 사전 줄로 바꿈 (품사가 없으면 `name`) |
| `web/examples/`, `web/examples.tsv` | 예제 텍스트와 메뉴 이름 |
| `web/build.mjs` | 사이트를 `build/web/dist`에 만듦 |
| `web/check.mjs` | 헤드리스 Chrome으로 사이트를 확인 |
| `web/deploy.sh` | 서버에 올림, 되돌림 |

- 사용자 사전 표와 이름 표를 합쳐 엔진의 `/userdict.tsv`(사용자 계층)로 쓰고 엔진을 다시 불러옵니다.
  불러오지 못하는 줄이 있으면 사용자 사전을 빼고 번역하며 오류를 보여 줍니다.
- 문서 방식은 입력 전체를 한 세션으로(`hdtrans`와 같음), 줄 단위는 줄마다 새 세션으로(`hdtrans -each`와
  같음) 번역합니다. 번역 전에 `hdTrans_Reset`으로 세션을 비웁니다.
- 페이지 아래 버전 줄은 빌드한 커밋의 날짜(Asia/Seoul, `yyyy-MM-dd HH:mm:ss`)와 짧은 해시입니다.

## 빌드

Node 22 이상과 Emscripten이 필요합니다.

```sh
emcmake cmake --preset wasm
cmake --build --preset wasm --parallel
npm --prefix web ci
node web/build.mjs
```

| 환경 변수 | 설명 |
|---|---|
| `HDTRANS_DICTS` | 올릴 사전 폴더 (기본 `dicts`) |
| `HDTRANS_WASM` | 엔진 빌드 폴더 (기본 `build/wasm`) |
| `HDTRANS_WEB_EXAMPLES` | 저장소에 없는 예제 텍스트 폴더. `examples.tsv`에 적힌 파일 중 `web/examples/`에 없는 것을 여기서 찾음 |

`build/web/dist`에는 `index.html`, `js/`, `examples/`, `manifest.json`과 내용 해시 폴더
`assets/<sha256 앞 16자>/`(엔진과 사전 파일)가 생기고, 텍스트와 wasm 파일마다 `.br`, `.gz` 압축본이
붙습니다. 서버는 브라우저가 받는 인코딩의 압축본을 보내고, `assets/`는 오래 캐시하게 합니다.

## 확인

```sh
python3 -m http.server -d build/web/dist 8000 &   # 압축본을 쓰지 않는 정적 서버도 됨
head -20 INPUT > in.txt
build/mac/hdtrans -dicts dicts -userdict '' -in in.txt > want.txt
node web/check.mjs http://127.0.0.1:8000/ INPUT want.txt 20
```

`check.mjs`는 두 번 방문해 준비 시간, 받은 바이트, 엔진 메모리를 적고, 입력 앞 20줄의 번역이 CLI 출력과
같은지, 긴 입력의 진행 막대, 사용자 사전·이름 표 편집과 붙여 넣기가 번역을 바꾸는지, 버전 줄을
확인하고, 번역 API가 없는지(`POST /api/translate`가 404) 봅니다(`http.server`는 501로 답해 이 항목만
실패합니다). `CHROME`으로 Chrome 경로를, `SHOT`으로 화면 저장 경로를 줍니다.

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

엔진은 TSV 사전 전체(약 82 MB)를 읽어 한 번에 컴파일합니다. 불러온 뒤 남는 사전은 약 44 MB지만
컴파일하는 동안 최대 약 240 MB를 쓰므로 WebAssembly 메모리가 약 270 MB까지 커지고, WebAssembly 메모리는
줄어들지 않습니다. 사용자 사전을 바꿀 때는 엔진을 내린 뒤 다시 불러오므로 더 커지지 않습니다.
