---
name: fix-issue
description: 마일스톤의 서브이슈 하나를 수정·테스트하고 PR을 만든 뒤, 새 컨텍스트에서 리뷰하고, 지적을 반영하거나 새 서브이슈로 등록하고, 무결성을 검증한 다음 개발자 확인을 받아 머지하고 이슈를 닫는다. 서브이슈 전용.
argument-hint: "[서브이슈 번호]"
disable-model-invocation: true
---

# 서브이슈 처리: 수정 → PR → 리뷰 → 반영 → 검증 → 머지

인자: `$ARGUMENTS` (서브이슈 번호, 비어 있을 수 있음)

먼저 `.claude/shared/workflow.md`를 읽는다. 저장소 구조, CI 범위, **서브이슈 등록**, **처리 순서**는 그
문서의 절차를 따른다. 헬퍼 `python .claude/shared/hydroid_gh.py`를 아래에서 `hgh`라고 쓴다.

아래 단계를 순서대로 끝까지 진행한다. 개발자에게 묻는 곳은 **질문 지점**으로 표시한 곳과, 진행할 수 없는
상황(테스트가 계속 실패, CI가 원인 불명으로 실패, base가 앞서 나감 등)뿐이다. 질문은 항상
AskUserQuestion으로 선택지를 만들어 묻고, 질문만 보고도 판단할 수 있게 배경을 한두 문장 넣는다.

사용자에게 보이는 글(PR, 리뷰, 답글, 이슈, 진행 보고)은 모두 한국어로 쓴다. 커밋 메시지만 영어다.

## 0. 준비

1. `gh repo view --json nameWithOwner`로 저장소를 확인한다.
2. `git status --porcelain`이 비어 있지 않으면 멈추고 개발자에게 알린다(남의 작업을 덮지 않는다).
3. `git fetch origin`.

## 1. 서브이슈 확인과 담당자

- `$ARGUMENTS`에 번호가 있으면 그것을 쓴다.
- 없으면 열린 마일스톤 중 가장 앞선 것의 메인 이슈에 대해 `hgh subs <P>`를 보고, 공용 절차 **처리 순서**의
  앞쪽 열린 서브이슈 최대 4개를 골라 **질문 지점**: "어느 서브이슈를 처리할까요?"(다른 번호는 Other).

`hgh kind <N>`으로 확인한다. **이 스킬은 서브이슈만 처리한다.**
- `kind`가 `main`이면 멈추고 "#<N>은 마일스톤 메인 이슈입니다. `/open-ms <N>` 또는 `/close-ms <N>`을 쓰세요."라고 알린다.
- `other`(parent가 없거나, parent가 메인 이슈가 아님)면 멈추고 그 이유를 알린다. 메인 이슈의 서브이슈로
  옮길지는 개발자가 정한다.
- 이미 닫혀 있으면 계속할지 묻는다.

담당자를 연결한다: `gh issue edit <N> --add-assignee i-technika`.

`gh issue view <N> --json number,title,body,milestone,labels,state,comments`로 이슈를 읽는다
(`--comments`와 `--json`은 함께 쓸 수 없다). parent(메인 이슈) 번호를 `<P>`로 기억한다.

## 2. base 브랜치와 작업 브랜치

base는 **마일스톤의 단계 브랜치**다. `kind`가 준 `step` `<k>`에 대해
`git ls-remote --heads origin "step<k>/*"`로 찾는다(예: R4 → `step4/resource-table`).

- 정확히 하나면 그것을 base로 쓴다.
- 없거나 여러 개면 **질문 지점**: 후보 브랜치를 선택지로 묻는다. 단계 브랜치가 없으면 `/open-ms <P>`가 아직
  안 돈 것이므로 그것을 먼저 권한다. `main`을 base로 고르면 그 PR은 CI가 full로 돈다.

작업 브랜치 `fix/<N>-<짧은-영문-slug>`를 **이슈에 연결된 브랜치로** 만든다. 이슈의 Development에 브랜치가
붙고, 이 브랜치에서 연 PR도 거기 연결된다:
```
gh issue develop <N> --base <base> --name fix/<N>-<slug> --checkout
```
같은 이름이 이미 있으면 이어서 쓸지, 새로 만들지 묻는다. 이어서 쓰면 `git switch`로 가져온다.

## 3. 수정과 테스트

1. 이슈의 재현 조건과 완료 기준을 코드에서 확인하고 고친다. 회귀를 막는 테스트를 함께 넣는다.
   주변 코드의 주석 밀도, 이름, 관용구를 따른다.
2. 빌드와 테스트(CI의 `core` job과 같음):
   ```
   cmake --preset debug -DHYDROID_RES_REQUIRE_LOCALE_TESTS=ON
   cmake --build --preset debug --parallel
   ctest --preset debug
   ```
   decimal-comma 로케일이 이 PC에 없어서 configure가 실패하면 `-DHYDROID_RES_REQUIRE_LOCALE_TESTS=ON`만
   빼고 다시 하고, PR의 테스트 항목에 그렇게 적는다.
3. 바꾼 `*.h`, `*.cpp`에 clang-format 23.1.5(`.clang-format`)로 `--dry-run --Werror`를 돌린다.
   서브이슈 PR의 CI(quick)에는 `clang-format` job이 없으므로 여기서 꼭 확인한다. 23.1.5가 없으면 건너뛰고
   PR에 "clang-format은 마일스톤 PR의 full CI에서 확인됩니다"라고 적는다.
4. golden(`golden/`, `scripts/golden/`)을 다시 만들어야 하면 해당 스크립트와 README의 조건(호스트 OS,
   build-tools 버전)을 지키고, 바뀐 행 수를 기록한다.
5. 테스트가 실제로 잡는지 보려고 파일을 임시로 바꿀 때는, 먼저 scratchpad에 사본을 두고 그 사본으로 되돌린다.
   `git checkout -- <file>`이나 `git stash`로 되돌리면 아직 커밋하지 않은 수정까지 지워진다.
6. 실패하면 원인을 고쳐 다시 돌린다. 같은 실패를 세 번 고쳐도 안 되면 **질문 지점**: 상황과 선택지
   (접근 바꾸기 / 여기서 중단 / 실패를 PR에 적고 계속)를 묻는다.
7. 작업하다가 이 이슈의 범위 밖 문제(다른 버그, 낡은 주석, 빠진 테스트 등)를 발견하면 고치지 말고
   **추가 이슈 목록**에 `파일:줄`, 문제, 제안을 적어 둔다. 8단계 뒤에 등록한다.

## 4. 커밋과 PR

커밋 메시지(영어, 기존 커밋과 같은 스타일):
- 제목은 무엇을 했는지 쉬운 말로, 끝에 ` (#<N>)`. 예: `Pin the OS the aapt2 golden files come from, and check it (#62)`
- 본문은 72자로 줄 바꿈하고, 왜 바꿨는지와 무엇을 확인했는지 적는다. 바뀐 곳이 여럿이면 `- ` 목록.
- 하네스가 알려 준 attribution 줄로 끝낸다.

`git push -u origin <branch>` 후 PR을 만든다. 담당자, label(이슈와 같은 것), 마일스톤(이슈와 같은 것),
프로젝트를 만들 때 함께 넣는다:
```
gh pr create --base <base> --head <branch> --title "<한국어 제목> (#<N>)" --body-file <scratchpad의 파일> \
  --assignee i-technika --label <이슈의 label마다 하나씩> --milestone "<이슈의 마일스톤 제목>" \
  --project "Hydroid Project"
```
이어서:
1. 프로젝트 필드: `hgh copy-fields <N> <M>` (단계·영역·플랫폼·res 릴리스를 이슈와 같게. Status는 자동화에 맡긴다).
2. **Development 연결 확인**: `hgh links <M>`의 `linkedIssues`에 `<N>`이 있어야 한다. 없으면 GitHub API로는
   연결할 방법이 없으므로, 개발자에게 바로 알린다: "PR #<M> 오른쪽 Development에서 #<N>을 연결해 주세요:
   <PR URL>". 기다리지 않고 진행하고, 9단계 검증에서 다시 본다.

PR 본문(한국어, 기존 PR과 같은 구성):
- `## 요약`: 이슈 번호와 무엇이 문제였는지. base가 기본 브랜치가 아니면
  `> base가 기본 브랜치가 아니라서 머지해도 #<N>은 자동으로 닫히지 않습니다.`를 넣는다.
- `## 변경`: 파일별로 무엇을 바꿨는지.
- `## 테스트`: 실제로 돌린 것만 `- [x]`로. 돌리지 못한 것은 `- [ ]`와 이유. CI는 quick 범위(Linux static,
  shared)만 돈다고 적는다.
- 하네스가 알려 준 PR attribution 줄로 끝낸다.

### 질문 지점: 개발자 문의

수정 중에 개발자가 정해야 할 것이 있었다면(설계 선택, 범위, 의도적 동작인지 여부 등) 여기서 묻는다.
질문마다 2~4개 선택지를 만들고, 권하는 것을 첫 번째에 `(권장)`으로 둔다. 한 번에 최대 4개까지.
답을 받으면 반영해서 빌드·테스트·커밋·push하고 PR 본문을 고친다(`gh pr edit --body-file`).
물을 것이 없으면 묻지 않고 넘어간다.

## 5. 리뷰 (새 컨텍스트)

수정한 맥락을 모르는 리뷰어가 보도록 **Agent 도구로 general-purpose 서브에이전트**를 띄운다. 프롬프트에는
아래만 넣고, 수정하면서 내린 판단이나 이유는 넣지 않는다(PR 본문과 이슈를 스스로 읽게 한다):

> 저장소 `<owner>/<repo>`의 PR #<M>(base `<base>`, 이슈 #<N>)을 리뷰해 줘. 너는 코드를 고치거나 커밋하지
> 않는다. 작업 트리는 이미 PR head에 있다.
>
> 1. `gh pr view <M>`, `gh pr diff <M>`, `gh issue view <N> --comments`를 읽고, 바뀐 코드 주변까지 읽는다.
> 2. `cmake --preset debug`, `cmake --build --preset debug --parallel`, `ctest --preset debug`로 빌드하고
>    테스트한다. 주장을 확인할 때는 실제로 돌려 보고, 임시로 바꾼 것은 반드시 되돌린다.
> 3. 정확성 버그, 이슈의 완료 기준을 못 채운 것, 테스트가 못 잡는 경우, 코드와 어긋난 주석·문서·README를 찾는다.
>    취향 문제는 넣지 않는다.
> 4. 각 지적마다 `gh issue list --state all --search "<핵심 단어>"`로 기존 이슈를 찾는다. 같은 문제를 다루는
>    열린 이슈가 있으면 그 지적은 **중복 #X**로 분류하고, "이번 PR에서는 다루지 않습니다. #X와 겹칩니다:
>    <이유>"라고 적는다.
> 5. 나머지는 **이번 PR**(이 이슈의 범위 안) 또는 **범위 밖**(이 이슈와 다른 문제)으로 분류한다.
> 6. 리뷰를 한 번에 올린다: `gh api repos/<owner>/<repo>/pulls/<M>/reviews --method POST --input <json>`,
>    `event: "COMMENT"`, `commit_id`는 PR head SHA.
>    - `body`: `## 리뷰`, `### 확인한 것`(실제로 돌린 것), `### 정리` 표(# / 항목 / 심각도 / 처리).
>    - `comments`: 지적 하나당 인라인 코멘트 하나(`path`, `line`, `side: "RIGHT"`). 제목 줄
>      `**<번호>. <요약> (<심각도>, <분류>)**`, 근거(파일:줄), `**제안**`. 줄이 diff 밖이면 리뷰를 올린 뒤
>      `gh api repos/<owner>/<repo>/pulls/<M>/comments -f subject_type=file -f path=<파일> -f commit_id=<SHA> -f body=...`로
>      그 파일에 따로 단다. 파일이 diff에 없으면 body에만 쓴다.
> 7. 마지막에 지적 목록을 이 형식으로 돌려줘: `번호 | 분류(이번 PR / 중복 #X / 범위 밖) | 심각도 | 파일:줄 | 한 줄 요약 | 제안`.
>    지적이 없으면 "지적 없음"과 확인한 것을 돌려줘.

## 6~8. 리뷰 반영

리뷰가 올라가면 스레드를 가져온다:
```
gh api graphql -f query='query($o:String!,$r:String!,$n:Int!){repository(owner:$o,name:$r){pullRequest(number:$n){reviewThreads(first:100){nodes{id isResolved path line comments(first:20){nodes{databaseId body}}}}}}}' -F o=<owner> -F r=<repo> -F n=<M>
```
서브에이전트가 돌려준 목록과 스레드를 번호로 맞춘다. 스레드 답글은
`gh api repos/<owner>/<repo>/pulls/<M>/comments/<첫 코멘트 databaseId>/replies -f body=...`,
Resolve는 `gh api graphql -f query='mutation($id:ID!){resolveReviewThread(input:{threadId:$id}){thread{isResolved}}}' -F id=<thread id>`.

1. **중복 #X**: 고치지 않는다. 답글 "이번 PR에서는 다루지 않습니다. #X에서 다룹니다: <이유>"를 달고 Resolve.
   답글에 `#X`를 적으면 이슈 쪽 타임라인에도 링크가 남는다.
2. **범위 밖**: 묻지 않고 공용 절차 **서브이슈 등록**으로 메인 이슈 `<P>`의 서브이슈를 만든다(출처
   `PR #<M> 리뷰 <번호>번에서 나온 항목입니다.`). 답글 "새 서브이슈 #Y로 분리했습니다"를 달고 Resolve.
   리뷰어가 범위 밖이라고 했어도 이 이슈의 완료 기준에 드는 것이면 **이번 PR**로 옮겨 3번으로 처리한다.
3. **이번 PR**: 고치고, 3단계처럼 빌드·테스트·포맷을 돌린다. 지적을 잘못 짚었다고 판단하면 고치지 않고 근거를
   답글로 단다. 반영한 지적을 한 커밋에 묶는다. 본문은 `From the review of PR #<M>:` 다음에 `- ` 목록.
   push한 뒤 스레드마다 짧은 답글("고쳤습니다(<짧은 SHA>): <무엇을>")을 달고 Resolve.
4. 3단계에서 적어 둔 **추가 이슈 목록**도 공용 절차 **서브이슈 등록**으로 등록한다(출처
   `#<N> 작업 중 발견한 항목입니다.`).
5. PR에 반영 요약 코멘트를 하나 단다(`gh pr comment`): `## 리뷰 반영 (<짧은 SHA>)`, 지적 번호별로
   고쳤는지/다루지 않는지와 이유, 새로 등록한 서브이슈, `확인한 것:` 목록, PR attribution 줄.

### 다시 리뷰

이번 PR에서 코드를 고쳤다면 5단계의 서브에이전트를 한 번 더 띄우되, 마지막 리뷰 이후 커밋
(`git diff <이전 head>..HEAD`)만 보게 한다. 새 지적이 있으면 6~8을 다시 한다. 리뷰는 **최대 2라운드**다.
2라운드 뒤에도 새 지적이 나오면 **질문 지점**: 한 번 더 반영할지, 지금 상태로 검증으로 갈지 묻는다.

## 9. 무결성 검증

모두 확인하고 결과를 표로 정리한다:

| 항목 | 확인 방법 |
| --- | --- |
| 작업 트리 깨끗함 | `git status --porcelain` 비어 있음 |
| push됨 | 로컬 `HEAD` == `gh pr view <M> --json headRefOid` |
| base | PR base가 2단계의 base와 같음 |
| base와 충돌 없음, 뒤처지지 않음 | `gh pr view <M> --json mergeable,mergeStateStatus`: `MERGEABLE`이고 `DIRTY`/`BEHIND` 아님 |
| 미해결 스레드 0 | 위 GraphQL에서 `isResolved: false` 없음 |
| PR 메타데이터 | `gh pr view <M> --json assignees,labels,milestone,projectItems`: `i-technika`, 이슈와 같은 label·마일스톤, `Hydroid Project` |
| Development | `hgh links <M>`에 `<N>` 있음 (없으면 4단계의 안내를 다시 하고 표에 ❌로 둔다) |
| 이슈 담당자 | `hgh kind <N>`의 `assignees`에 `i-technika` |
| 로컬 빌드·테스트 | HEAD에서 3단계의 빌드와 `ctest` 통과 |
| CI (quick) | `gh pr checks <M> --watch`(백그라운드). `core (ubuntu-24.04)`, `core (ubuntu-24.04, shared)` 두 개 통과 |

- CI가 실패하면 로그(`gh run view <id> --log-failed`)를 보고, 이 PR 때문이면 고쳐서 push하고 다시 확인한다.
  러너나 네트워크 같은 외부 원인으로 보이면 **질문 지점**: 다시 돌릴지(`gh run rerun --failed`), 중단할지 묻는다.
- `BEHIND`/`DIRTY`이면 **질문 지점**: base를 merge해 올릴지, 중단할지 묻는다.

### 질문 지점: 머지 확인 (항상 묻는다)

검증 표, 커밋 목록, 리뷰 처리 요약(고친 것 / 중복으로 넘긴 것 / 새 서브이슈)을 보여 주고 묻는다:
- `머지` — `gh pr merge <M> --merge` (기존과 같은 merge commit)
- `중단` — 여기서 멈추고 PR은 열어 둔다

머지 후:
1. `gh issue view <N> --json state`로 확인하고, 열려 있으면 닫는다:
   `gh issue close <N> --comment "#<M>에서 고쳤고, \`<base>\`에 머지했습니다(기본 브랜치가 아니라서 직접 닫습니다)."`
   리뷰에서 다른 이슈로 넘긴 것이 있으면 한 문장 덧붙인다(예: "리뷰에서 나온 ...는 #X에서 다룹니다.").
2. PR이 `MERGED`인지 확인한다.

## 10. 마무리와 처리 순서

1. 마지막 보고: PR 링크, 머지 커밋, 닫은 이슈, 새로 등록한 서브이슈(표), 다루지 않은 지적.
2. 공용 절차 **처리 순서**로 메인 이슈 `<P>`의 남은 서브이슈 순서를 다시 매겨 표로 보여 준다. 열린 서브이슈가
   없으면 "R<k>의 서브이슈를 모두 처리했습니다. 다음: `/close-ms <P>`"라고 적는다.
3. PushNotification으로 한 줄 알린다. 예: `#64 머지 완료 (PR #65), 새 서브이슈 #66`.
   질문 지점은 따로 알리지 않는다(질문이 뜰 때 소리 훅과 모바일 알림이 이미 나간다).
