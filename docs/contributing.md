# 기여와 릴리스

## 브랜치와 병합

- 작업 브랜치는 `develop`에서 만들고, PR도 `develop`으로 보냅니다.
- 일반 PR은 **Squash and merge**로 합칩니다. PR 제목이 최종 커밋 제목이 됩니다.
- 정식 릴리스에 넣을 변경은 `develop` → `main` PR로 모읍니다. 이 PR은 **Create a merge commit**으로 합쳐 개별 변경의 제목을 보존합니다.
- release-please가 만든 릴리스 PR만 예외로 `main`에 직접 합칩니다.

오역 수정과 검증 방법은 [fixing-translations.md](fixing-translations.md)에 있습니다.

## 커밋과 PR 제목

[Conventional Commits](https://www.conventionalcommits.org/ko/v1.0.0/) 형식을 씁니다. 커밋과 PR 제목 모두 같은 규칙입니다.

```text
종류(범위): 변경 내용
```

범위는 생략할 수 있고, 변경 내용은 한국어나 영어로 씁니다.

| 종류 | 용도 | 버전 변화 |
|---|---|---|
| `fix` | 오역, 오류 수정 | patch: 0.1.0 → 0.1.1 |
| `feat` | 새 기능 | minor: 0.1.0 → 0.2.0 |
| `perf` | 성능 개선 | patch |
| `docs` | 문서 | 단독으로 릴리스하지 않음 |
| `refactor`, `style` | 동작을 유지하는 구조, 형식 수정 | 단독으로 릴리스하지 않음 |
| `test`, `build`, `ci`, `chore` | 테스트, 빌드, 자동화, 기타 관리 | 단독으로 릴리스하지 않음 |
| `revert` | 기존 변경 취소 | 취소 대상에 따라 결정 |

```text
fix(rules): 조건 뒤 인용 번역 수정
feat(cli): 사용자 사전 여러 개 지원
perf(dict): 사전 로딩 시간 단축
docs: Windows 빌드 설명 보완
ci: 릴리스 자동화 추가
```

호환되지 않는 변경에는 `!`를 붙입니다. `major:`라는 종류는 쓰지 않습니다.

```text
feat(api)!: 초기화 함수의 인자 변경
```

커밋 본문에 `BREAKING CHANGE: 기존 호출 코드를 수정해야 함`을 적어도 됩니다.
Squash할 때는 이 본문도 남기거나 PR 제목에 `!`를 붙입니다. **0.x에서도 하위 호환성을 깨는 변경은 major를 올립니다**(0.1.0 → 1.0.0).
PR 제목은 CI에서 검사합니다. 원인, 테스트, 번역 판정 숫자는 제목 대신 본문에 적습니다.

## 릴리스가 묶이는 방식

1. `develop`에 일반 PR을 모읍니다. 이때 갱신되는 배포판은 `nightly`입니다.
2. `develop`을 `main`에 병합하면 release-please가 다음 버전의 릴리스 PR 하나를 만들거나 갱신합니다.
3. 릴리스 PR을 열어 둔 동안 `main`에 들어온 변경도 같은 PR에 쌓입니다. 버전은 가장 큰 변경을 따릅니다.
4. 릴리스 PR을 합치면 `CHANGELOG.md`, `release-version.txt`, 버전 목록이 갱신되고 `vX.Y.Z` 태그와 GitHub Release가 만들어집니다.
5. 해당 태그로 모든 플랫폼을 빌드하고 테스트한 뒤 압축 파일과 `SHA256SUMS.txt`를 릴리스에 첨부합니다.
6. `publish-npm.yml`이 같은 CI의 npm 패키지를 OIDC로 직접 게시합니다. nightly와 `dry_run=true` 빌드는 npm에 게시하지 않습니다.

첫 정식 버전은 `0.1.0`입니다. 그 뒤 `fix` 여러 개는 patch 한 번, `feat`와 `fix`가 함께 있으면 minor 한 번 올라갑니다.
릴리스 PR은 자동 병합하지 않습니다. 문서나 CI 변경만으로는 새 정식 버전을 만들지 않습니다.
릴리스 페이지가 먼저 만들어지고, 빌드가 끝나면 첨부 파일이 올라옵니다.

## 저장소 관리자 설정

Settings → Actions → General에서 **Allow GitHub Actions to create and approve pull requests**를 켭니다.
워크플로는 기본 `GITHUB_TOKEN`을 쓰므로 별도 개인 토큰은 필요 없습니다.
이 토큰으로 만든 PR에는 PR 이벤트 CI가 자동 실행되지 않습니다. 릴리스 PR의 버전과 변경 내역은 병합 전에 직접 확인합니다.

토큰으로 만든 태그도 push 이벤트를 일으키지 않으므로, `release-please.yml`이 **Build**를 해당 태그로 명시적으로 실행합니다.
첨부 파일 빌드가 실패하면 문제를 확인한 뒤 **Build → Run workflow**에서 같은 태그를 선택하고 `dry_run=false`로 다시 실행합니다.
정식 배포는 `main`에 포함된 `vX.Y.Z` 태그에서만 허용합니다. 기존 릴리스 노트는 첨부 파일을 올릴 때 보존합니다.
npm 게시만 실패했다면 **Publish npm → Run workflow**에서 같은 태그와 성공한 **Build**의 실행 ID를 지정합니다.
