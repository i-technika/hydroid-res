---
name: close-ms
description: 마일스톤 메인 이슈 하나로 단계를 닫는다. 단계 브랜치에 full CI(15개 job)를 돌리고, 이상이 없으면 단계 PR을 main에 머지해 메인 이슈와 마일스톤을 닫는다. CI가 오류로 실패하면 닫지 않고 서브이슈를 등록해 처리 순서를 보여 주고, 지연 등 오류가 아닌 실패면 실패한 job만 다시 돌린다. 메인 이슈 전용.
argument-hint: "[메인 이슈 번호]"
disable-model-invocation: true
---

# 마일스톤 닫기: full CI → main 머지 → 이슈·마일스톤 닫기

인자: `$ARGUMENTS` (마일스톤 메인 이슈 번호, 비어 있을 수 있음)

먼저 `.claude/shared/workflow.md`를 읽는다. 헬퍼 `python .claude/shared/hydroid_gh.py`를 아래에서 `hgh`라고 쓴다.
질문은 AskUserQuestion으로, 배경을 한두 문장 넣고, 권하는 선택지를 첫 번째에 `(권장)`으로 둔다.
사용자에게 보이는 글은 한국어다.

개발자는 "이상이 없으면 main에 머지하고 이슈를 닫는다"를 이 스킬을 부르는 것으로 이미 정했다. 그래서 CI가
모두 통과하고 아래 확인이 모두 맞으면 **묻지 않고 머지한다.** 하나라도 어긋나면 머지하지 않는다.

## 0. 준비

1. `gh repo view --json nameWithOwner`, `git status --porcelain`(비어 있지 않으면 멈춤), `git fetch origin`.

## 1. 메인 이슈와 단계 PR

- `$ARGUMENTS`에 번호가 없으면 열린 마일스톤의 메인 이슈를 최대 4개 골라 **질문 지점**: "어느 마일스톤을 닫을까요?"
- `hgh kind <N>`이 `main`이 아니면 멈춘다. **이 스킬은 메인 이슈만 처리한다.** 서브이슈면 `/fix-issue <N>`을 권한다.
- 단계 PR을 찾는다: `gh pr list --state open --base main --json number,headRefName,closingIssuesReferences`에서
  head가 `step<k>/`로 시작하는 것(`<k>`는 `kind`의 `step`). 없으면 멈추고 `/open-ms <N>`을 권한다. 여럿이면 묻는다.
- 단계 PR을 `<M>`, 단계 브랜치를 `<branch>`라고 한다.

## 2. 닫기 전 확인

| 항목 | 확인 방법 | 어긋나면 |
| --- | --- | --- |
| 서브이슈 | `hgh subs <N>`에 `OPEN` 없음 | **질문 지점** (아래) |
| 미해결 리뷰 스레드 | `/fix-issue` 6~8단계의 GraphQL에서 `isResolved: false` 없음 | 목록을 보여 주고 멈춤 |
| main과 충돌 없음, 뒤처지지 않음 | `gh pr view <M> --json mergeable,mergeStateStatus`: `MERGEABLE`, `DIRTY`/`BEHIND` 아님 | **질문 지점**: main을 `<branch>`에 merge해 올릴지(충돌은 해결하고 빌드·테스트), 멈출지 |
| 서브이슈 PR이 남지 않음 | `gh pr list --state open --base <branch>` 비어 있음 | 목록을 보여 주고 멈춤 |
| PR 메타데이터 | 담당자 `i-technika`, label, 마일스톤, `Hydroid Project`, `hgh links <M>`에 `<N>` | 빠진 것을 채운다(`gh pr edit`, `hgh copy-fields <N> <M>`). Development만 API로 못 채우므로 PR URL과 함께 연결을 부탁하고, PR 본문의 `Closes #<N>`이 있으면 진행 |

**질문 지점: 열린 서브이슈** — 열린 서브이슈 표(`# / Type / 제목`)를 보여 주고 묻는다:
- `중단 (권장)` — `/fix-issue`로 먼저 처리한다.
- `다음 마일스톤으로 옮기고 진행` — 각 서브이슈를 다음 마일스톤 메인 이슈의 서브이슈로 옮기고
  (`gh api repos/<o>/<r>/issues/<새 P>/sub_issues -F sub_issue_id=<id> -F replace_parent=true`,
  `gh issue edit <#> --milestone "<다음 마일스톤>"`, `hgh copy-fields <새 P> <#>`), 이슈에 옮긴 이유를 코멘트한다.
- `그대로 진행` — 열린 채로 두고 닫는다(마일스톤은 닫지 않는다).

## 3. full CI

로컬을 단계 브랜치 최신으로 맞추고(`git switch <branch>`, `git pull --ff-only`) 단계 브랜치 HEAD에 full CI를 돌린다:
```
gh workflow run ci.yml --ref <branch> -f scope=full
```
- 실행 id는 `gh run list --workflow ci.yml --branch <branch> --event workflow_dispatch --limit 1 --json databaseId,headSha,status`로
  찾는다. `headSha`가 `git rev-parse HEAD`와 같은 실행이 나올 때까지 확인한다.
- `gh workflow run`이 "workflow_dispatch 트리거가 없다"고 실패하면(main의 ci.yml이 아직 옛것), 단계 PR의
  최신 `pull_request` 실행을 다시 돌린다: `gh run rerun <id>`. base가 main이라 그 실행도 full이다.
- 공용 절차 **CI 범위**대로 백그라운드로 기다리고, **15개 job 이름이 모두 있고 모두 `success`**인지 확인한다.
  단계 PR 자체의 체크(`gh pr checks <M>`)도 모두 통과해야 한다.

### CI가 실패하면

실패한 job마다 `gh run view <id> --log-failed`와 `gh run view <id> --json jobs`(각 job의 `conclusion`, 실패한
step)를 보고 **오류**인지 **오류가 아닌 실패**인지 가린다.

| 구분 | 판단 근거 |
| --- | --- |
| 오류 | 빌드 실패(컴파일·링크 오류, 경고가 오류로), 테스트 실패, configure 실패(의존성 버전, 옵션), clang-format 차이, `locale tables`의 `git diff` 차이, 검사 step(export, SONAME, 새니타이저 기호 등)의 실패. 같은 커밋에서 다시 돌려도 같은 결과가 나올 것 |
| 오류가 아닌 실패 | `conclusion`이 `cancelled`·`timed_out`, 러너를 잃음(`The runner has received a shutdown signal`, `lost communication`), 내려받기 실패(checkout, FetchContent, apt, pipx, Gradle, `locale tables`의 android.googlesource.com 내려받기에서 네트워크·timeout·HTTP 5xx·429), 캐시 서비스 오류, 대기열 지연으로 시작하지 못함. 코드와 상관없이 다시 돌리면 지나갈 것 |

둘이 섞여 있으면 오류로 처리한다. 어느 쪽인지 가릴 수 없으면 오류가 아닌 실패로 보고 한 번 다시 돌려 본다.

**오류가 아닌 실패** — **질문 지점**: 실패한 job 이름과 근거(로그 한 줄)를 보여 주고 묻는다.
- `실패한 job만 다시 실행 (권장)` — `gh run rerun <id> --failed`. 통과한 job은 다시 돌지 않는다. 다시 돈 job을
  백그라운드로 기다린 뒤, 15개 job이 모두 `success`인지 처음부터 다시 확인한다. 다시 돌린 job이 또
  실패하면 이 절을 처음부터 다시 적용한다(같은 job이 두 번째로 같은 이유로 실패하면 오류일 수 있다고 보고에 적는다).
- `중단` — 머지하지 않고 멈춘다. 다음에 `/close-ms <N>`을 다시 부르면 된다.

**오류** — 머지하지 않고, 메인 이슈와 마일스톤도 **닫지 않는다.**
1. 원인 하나당 서브이슈 하나를 공용 절차 **서브이슈 등록**으로 메인 이슈 `<N>`에 등록한다. 여러 job이 같은
   원인으로 실패했으면(예: 같은 컴파일 오류가 세 OS에서) 하나로 묶고, 본문에 실패한 job을 모두 적는다.
   - `## 문제`에 실패한 job 이름, step, 오류 메시지(로그 몇 줄을 코드 블록으로), 관련 `파일:줄`.
   - 출처: `/close-ms 의 full CI(<run URL>) <job 이름>에서 나온 항목입니다.`
2. 공용 절차 **처리 순서**로 메인 이슈 `<N>`의 남은 서브이슈(방금 등록한 것 포함) 순서를 표로 보여 준다.
   CI를 깨는 이슈는 단계를 닫는 것을 막으므로, 다른 의존이 없으면 맨 앞에 둔다.
3. 단계 PR에 코멘트를 단다(`gh pr comment <M>`): `## /close-ms: full CI 실패`, 실행 링크, 실패한 job과
   등록한 서브이슈, "서브이슈를 처리한 뒤 `/close-ms <N>`을 다시 합니다."
4. 보고하고 멈춘다: 실패한 job 표, 등록한 서브이슈, 처리 순서 표, "다음: `/fix-issue <1번 이슈>`, 모두 끝나면
   `/close-ms <N>`". PushNotification으로 한 줄 알린다. 예: `R4 닫기 보류: full CI 실패, 서브이슈 #120 등록`.

## 4. main에 머지하고 닫기

2·3단계가 모두 맞으면 묻지 않고 진행한다.

1. 머지 직전에 `gh pr view <M> --json headRefOid,mergeStateStatus`로 HEAD가 CI를 돌린 SHA와 같고
   `mergeStateStatus`가 `CLEAN` 또는 `HAS_HOOKS`인지 다시 본다. 다르면 3단계부터 다시 한다.
2. `gh pr merge <M> --merge` (기존 단계 PR과 같은 merge commit). 단계 브랜치는 지우지 않는다.
3. 메인 이슈: `gh issue view <N> --json state`. `Closes #<N>`으로 닫혔으면 그대로 두고, 열려 있으면
   `gh issue close <N> --comment "#<M>을 main에 머지해 R<k>를 마칩니다. full CI(15개 job): <run URL>"`.
4. 마일스톤: 열린 이슈가 0이면 닫는다:
   `gh api -X PATCH repos/<o>/<r>/milestones/<마일스톤 번호> -f state=closed`.
   2단계에서 `그대로 진행`을 골라 열린 이슈가 남았으면 닫지 않고 보고에 적는다.
5. `gh pr view <M> --json state,mergeCommit`이 `MERGED`인지 확인한다.

## 5. 마무리

1. 보고: 단계 PR, 머지 커밋, full CI 실행 링크와 15개 job 결과 표, 닫은 메인 이슈와 마일스톤, 옮기거나 남긴 서브이슈.
2. 다음 마일스톤의 메인 이슈를 찾아 "다음: `/open-ms <번호>`"를 적는다.
3. PushNotification으로 한 줄 알린다. 예: `R4 완료: PR #77 main 머지, #96·마일스톤 닫음`.
