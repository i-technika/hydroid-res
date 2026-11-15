---
name: fix-issue
description: 이슈 하나를 수정·테스트하고 PR을 만든 뒤, 새 컨텍스트에서 리뷰하고, 지적을 반영해 Resolve하고, 무결성을 검증한 다음 개발자 확인을 받아 머지하고 이슈를 닫는다.
argument-hint: "[이슈 번호]"
disable-model-invocation: true
---

# 이슈 처리: 수정 → PR → 리뷰 → 반영 → 검증 → 머지

인자: `$ARGUMENTS` (이슈 번호, 비어 있을 수 있음)

아래 단계를 순서대로 끝까지 진행한다. 개발자에게 묻는 곳은 **질문 지점**으로 표시한 곳과, 진행할 수 없는
상황(테스트가 계속 실패, CI가 원인 불명으로 실패, base가 앞서 나감 등)뿐이다. 질문은 항상
AskUserQuestion으로 선택지를 만들어 묻고, 질문만 보고도 판단할 수 있게 배경을 한두 문장 넣는다.

사용자에게 보이는 글(PR, 리뷰, 답글, 이슈, 진행 보고)은 모두 한국어로 쓴다. 커밋 메시지만 영어다.

## 0. 준비

1. `gh repo view --json nameWithOwner`로 저장소를 확인한다.
2. `git status --porcelain`이 비어 있지 않으면 멈추고 개발자에게 알린다(남의 작업을 덮지 않는다).
3. `git fetch origin`.

## 1. 이슈 번호

- `$ARGUMENTS`에 번호가 있으면 그것을 쓴다.
- 없으면 `gh issue list --state open --limit 4 --json number,title,milestone`로 최근 열린 이슈를 골라
  **질문 지점**: "어느 이슈를 처리할까요?" 선택지로 보여 준다(다른 번호는 Other로 입력).

`gh issue view <N> --json number,title,body,milestone,labels,state,comments`로 이슈를 읽는다
(`--comments`와 `--json`은 함께 쓸 수 없다).
이미 닫혀 있으면 계속할지 묻는다.

## 2. base 브랜치와 작업 브랜치

base는 **이슈의 마일스톤과 맞는 브랜치**다. 마일스톤 제목 `R<k>: ...`의 `<k>`에 대해
`git ls-remote --heads origin "step<k>/*"`로 찾는다(예: `R3: Config·폴더 이름 파서` → `step3/config`).

- 정확히 하나면 그것을 base로 쓴다.
- 없거나, 여러 개이거나, 이슈에 마일스톤이 없으면 **질문 지점**: 후보 브랜치(`main` 포함)를 선택지로 묻는다.

작업 브랜치는 `fix/<N>-<짧은-영문-slug>`이고 `origin/<base>`에서 만든다. 같은 이름이 이미 있으면
이어서 쓸지, 새로 만들지 묻는다.

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
   23.1.5가 없으면 건너뛰고 PR에 그렇게 적는다(CI의 `format` job이 확인한다).
4. golden(`golden/`, `scripts/golden/`)을 다시 만들어야 하면 해당 스크립트와 README의 조건(호스트 OS,
   build-tools 버전)을 지키고, 바뀐 행 수를 기록한다.
5. 테스트가 실제로 잡는지 보려고 파일을 임시로 바꿀 때는, 먼저 scratchpad에 사본을 두고 그 사본으로 되돌린다.
   `git checkout -- <file>`이나 `git stash`로 되돌리면 아직 커밋하지 않은 수정까지 지워진다.
6. 실패하면 원인을 고쳐 다시 돌린다. 같은 실패를 세 번 고쳐도 안 되면 **질문 지점**: 상황과 선택지
   (접근 바꾸기 / 여기서 중단 / 실패를 PR에 적고 계속)를 묻는다.

## 4. 커밋과 PR

커밋 메시지(영어, 기존 커밋과 같은 스타일):
- 제목은 무엇을 했는지 쉬운 말로, 끝에 ` (#<N>)`. 예: `Pin the OS the aapt2 golden files come from, and check it (#62)`
- 본문은 72자로 줄 바꿈하고, 왜 바꿨는지와 무엇을 확인했는지 적는다. 바뀐 곳이 여럿이면 `- ` 목록.
- 하네스가 알려 준 attribution 줄로 끝낸다.

`git push -u origin <branch>` 후 PR을 만든다:
```
gh pr create --base <base> --head <branch> --title "<한국어 제목> (#<N>)" --body-file <scratchpad의 파일>
```
PR 본문(한국어, 기존 PR과 같은 구성):
- `## 요약`: 이슈 번호와 무엇이 문제였는지. base가 기본 브랜치가 아니면
  `> base가 기본 브랜치가 아니라서 머지해도 #<N>은 자동으로 닫히지 않습니다.`를 넣는다.
- `## 변경`: 파일별로 무엇을 바꿨는지.
- `## 테스트`: 실제로 돌린 것만 `- [x]`로. 돌리지 못한 것은 `- [ ]`와 이유.
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
2. **범위 밖** — **질문 지점**: 지적마다 하나의 질문으로 묻는다(한 번에 최대 4개):
   - `새 이슈로 분리 (권장)`: 이슈를 만든다(한국어, 기존 이슈처럼 제목에 문제와 결과를 쓰고, 본문에 재현·근거·제안,
     마일스톤은 맞는 것으로). 답글 "새 이슈 #Y로 분리했습니다"를 달고 Resolve.
   - `이번 PR에서 수정`: 3번으로 넘긴다.
   - `반영하지 않음`: 답글로 이유를 적고 Resolve.
   권장 표시는 실제 판단에 맞게 옮긴다.
3. **이번 PR**: 고치고, 3단계처럼 빌드·테스트·포맷을 돌린다. 지적을 잘못 짚었다고 판단하면 고치지 않고 근거를
   답글로 단다. 반영한 지적을 한 커밋에 묶는다. 본문은 `From the review of PR #<M>:` 다음에 `- ` 목록.
   push한 뒤 스레드마다 짧은 답글("고쳤습니다(<짧은 SHA>): <무엇을>")을 달고 Resolve.
4. PR에 반영 요약 코멘트를 하나 단다(`gh pr comment`): `## 리뷰 반영 (<짧은 SHA>)`, 지적 번호별로
   고쳤는지/다루지 않는지와 이유, `확인한 것:` 목록, PR attribution 줄.

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
| 로컬 빌드·테스트 | HEAD에서 3단계의 빌드와 `ctest` 통과 |
| CI | `gh pr checks <M> --watch`(오래 걸리므로 백그라운드로 돌리고 끝날 때 알림을 받는다). 모든 체크 통과 |

- CI가 실패하면 로그(`gh run view <id> --log-failed`)를 보고, 이 PR 때문이면 고쳐서 push하고 다시 확인한다.
  러너나 네트워크 같은 외부 원인으로 보이면 **질문 지점**: 다시 돌릴지(`gh run rerun --failed`), 중단할지 묻는다.
- `BEHIND`/`DIRTY`이면 **질문 지점**: base를 merge해 올릴지, 중단할지 묻는다.

### 질문 지점: 머지 확인 (항상 묻는다)

검증 표, 커밋 목록, 리뷰 처리 요약(고친 것 / 중복으로 넘긴 것 / 새 이슈)을 보여 주고 묻는다:
- `머지` — `gh pr merge <M> --merge` (기존과 같은 merge commit)
- `중단` — 여기서 멈추고 PR은 열어 둔다

머지 후:
1. `gh issue view <N> --json state`로 확인하고, 열려 있으면 닫는다:
   `gh issue close <N> --comment "#<M>에서 고쳤고, \`<base>\`에 머지했습니다(기본 브랜치가 아니라서 직접 닫습니다)."`
   리뷰에서 다른 이슈로 넘긴 것이 있으면 한 문장 덧붙인다(예: "리뷰에서 나온 ...는 #X에서 다룹니다.").
2. PR이 `MERGED`인지 확인한다.
3. 마지막 보고: PR 링크, 머지 커밋, 닫은 이슈, 새로 만든 이슈, 다루지 않은 지적.
4. PushNotification으로 한 줄 알린다. 예: `#64 머지 완료 (PR #65), 새 이슈 #66`.
   질문 지점은 따로 알리지 않는다(질문이 뜰 때 소리 훅과 모바일 알림이 이미 나간다).
