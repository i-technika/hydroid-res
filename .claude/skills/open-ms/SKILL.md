---
name: open-ms
description: 마일스톤 메인 이슈 하나로 단계를 연다. main에서 단계 브랜치를 만들어 마일스톤 전체를 구현하고 main으로 가는 PR을 만들어 full CI(16개 job)를 통과시킨 뒤, 별도 리뷰어 에이전트의 적대적 리뷰로 나온 문제를 서브이슈로 등록하고 처리 순서를 보여 준다. 메인 이슈 전용.
argument-hint: "[메인 이슈 번호]"
disable-model-invocation: true
---

# 마일스톤 열기: 단계 브랜치 → 전체 구현 → full CI → 적대적 리뷰 → 서브이슈

인자: `$ARGUMENTS` (마일스톤 메인 이슈 번호, 비어 있을 수 있음)

먼저 `.claude/shared/workflow.md`를 읽는다. 저장소 구조, CI 범위, **서브이슈 등록**, **처리 순서**는 그
문서의 절차를 따른다. 헬퍼 `python .claude/shared/hydroid_gh.py`를 아래에서 `hgh`라고 쓴다.

개발자에게 묻는 곳은 **질문 지점**과 진행할 수 없는 상황뿐이다. 질문은 AskUserQuestion으로, 배경을 한두
문장 넣고, 권하는 선택지를 첫 번째에 `(권장)`으로 둔다. 사용자에게 보이는 글은 한국어, 커밋 메시지만 영어다.

## 0. 준비

1. `gh repo view --json nameWithOwner`로 저장소를 확인한다.
2. `git status --porcelain`이 비어 있지 않으면 멈추고 알린다.
3. `git fetch origin`.

## 1. 메인 이슈 확인과 담당자

- `$ARGUMENTS`에 번호가 없으면 열린 마일스톤의 메인 이슈(제목 `R<k>:`, parent 없음)를 번호순으로 최대 4개
  골라 **질문 지점**: "어느 마일스톤을 열까요?"
- `hgh kind <N>`이 `main`이 아니면 멈춘다. **이 스킬은 메인 이슈만 처리한다.** 서브이슈면
  "#<N>은 #<P>의 서브이슈입니다. `/fix-issue <N>`을 쓰세요."라고 알린다.
- 닫혀 있으면 멈추고 알린다.

담당자를 연결한다: `gh issue edit <N> --add-assignee i-technika`. 단계 번호 `<k>`는 `kind`의 `step`이다.

## 2. 범위 파악

다음을 읽고 이 단계에서 만들 것과 완료 기준을 정리한다.
- 메인 이슈 본문과 코멘트(`gh issue view <N> --comments`). `## 내용`, `## 완료 기준`, `## 릴리스`.
- 본문이 링크한 로드맵 아티팩트(`claude.ai/artifact/...`)를 **Artifact 도구의 `read`**로 읽고, 이 단계(R<k>)와
  앞뒤 단계의 설계를 확인한다.
- 이미 있는 서브이슈(`hgh subs <N>`). 미리 등록된 결정 사항(`question` label)이 있으면 3단계에서 묻는다.
- 앞 단계의 단계 PR 본문(예: `gh pr view 77`)과 README, 기존 코드 구조, golden 스크립트.

## 3. 단계 브랜치

1. **앞 단계가 main에 들어갔는지**: R<k-1>의 단계 PR(`gh pr list --state all --base main --head "step<k-1>/..."`)이
   `MERGED`인지 본다. 아직 열려 있으면 **질문 지점**: "R<k-1>이 아직 main에 머지되지 않았습니다.
   - `main에서 시작 (권장)` — 앞 단계 코드 없이 시작. 앞 단계를 쓰지 않는 범위일 때.
   - `먼저 /close-ms <앞 단계 메인 이슈>` — 여기서 멈춘다.
   - `R<k-1> 브랜치 위에서 시작` — 앞 단계가 머지되면 main을 merge해 올린다."
   권장은 이번 단계가 앞 단계 코드를 쓰는지에 맞춰 옮긴다.
2. 브랜치 이름은 `step<k>/<영문-slug>`이다. slug는 이 단계의 핵심을 1~2 단어 kebab-case로(예: R3 `config`,
   R4 `resource-table`, R5라면 `qualifier-match`). `git ls-remote --heads origin "step<k>/*"`로 이미 있는지 본다.
   - 없으면 base `main`(1번에서 다르게 정했으면 그것)에서 **메인 이슈에 연결된 브랜치로** 만든다:
     ```
     gh issue develop <N> --base main --name step<k>/<slug> --checkout
     ```
   - 이미 있으면 **질문 지점**: 이어서 쓸지(`git switch`), 멈출지 묻는다.

### 질문 지점: 설계 결정

구현 전에 로드맵만으로 정할 수 없는 것(API 모양, aapt2와 다르게 갈지, 범위 경계, 미리 등록된 `question`
서브이슈)을 한 번에 최대 4개까지 묻는다. 물을 것이 없으면 넘어간다. 답은 해당 `question` 서브이슈에
코멘트로 남기고, 결정으로 끝나는 이슈면 닫는다.

## 4. 전체 구현

1. 단계 전체를 구현한다. 공개 헤더(`include/hydroid/res/`), 구현(`src/`), 도구(`tools/`), 테스트(`tests/`),
   필요하면 golden(`golden/`, `scripts/golden/`, README의 호스트 OS·build-tools 조건을 지킨다)과 README.
   규칙은 aapt2(android17-release 소스, build-tools 37.0.0의 실제 출력)를 따르고, 다르게 간 곳은 PR 본문의
   "설계 문서와 달라진 점"에 적는다.
2. 의미 있는 단위로 나눠 커밋한다. 메시지는 영어, 기존 커밋 스타일, 제목 끝에 ` (#<N>)`, attribution 줄로 끝낸다.
3. 로컬 확인(커밋 단위마다, 마지막에 한 번 더):
   ```
   cmake --preset debug -DHYDROID_RES_REQUIRE_LOCALE_TESTS=ON
   cmake --build --preset debug --parallel
   ctest --preset debug
   ```
   바꾼 `*.h`, `*.cpp`에 clang-format 23.1.5 `--dry-run --Werror`(없으면 CI의 `clang-format` job에 맡긴다).
   로케일이 없으면 `/fix-issue` 3단계와 같이 다룬다.
4. 같은 실패를 세 번 고쳐도 안 되면 **질문 지점**: 접근 바꾸기 / 중단 / 실패를 PR에 적고 계속.
5. 구현하면서 이번 단계에 넣지 않기로 한 것, 발견한 다른 문제는 **추가 이슈 목록**에 적어 둔다(7단계에서 등록).

## 5. 단계 PR과 full CI

`git push -u origin step<k>/<slug>` 후 PR을 만든다:
```
gh pr create --base main --head step<k>/<slug> --title "<k>단계: <내용>" --body-file <scratchpad의 파일> \
  --assignee i-technika --label <메인 이슈의 label> --milestone "<마일스톤 제목>" --project "Hydroid Project"
```
- 본문(#77과 같은 구성): `## 요약`(로드맵 링크, 단계 이름, 완료 기준, 부분별 표 `부분 / 하는 일 / aapt2에서
  해당하는 것`), `## 골든 데이터`(있으면), `## 설계 문서와 달라진 점`, `## 테스트`(실제로 돌린 것만 `- [x]`),
  그리고 `Closes #<N>` 줄(base가 main이라 머지하면 메인 이슈가 닫힌다). PR attribution 줄로 끝낸다.
- `hgh copy-fields <N> <M>`, `hgh links <M>`에 `<N>`이 있는지 확인. 없으면 `/fix-issue` 4단계처럼 알린다.

base가 main이므로 CI는 **full 범위(16개 job)**로 돈다. 공용 절차 **CI 범위**대로 백그라운드로 기다리고,
job 이름과 수가 목록과 맞는지 확인한다. 실패하면 원인을 고쳐 push하고 다시 확인한다. 러너·네트워크 같은
외부 원인이면 **질문 지점**: 다시 돌릴지(`gh run rerun --failed`), 중단할지.

## 6. 적대적 리뷰 (별도 에이전트)

구현한 맥락을 모르는 리뷰어가 보도록 **Agent 도구로 general-purpose 서브에이전트**를 띄운다. 구현하며 내린
판단과 이유는 넣지 않는다. diff가 크면(대략 3000줄 이상) 영역별(예: 파서 / 테이블 / 도구·빌드)로 나눠
같은 프롬프트로 2~3개를 병렬로 띄우고, 각자 맡은 경로를 프롬프트에 적는다.

> 저장소 `<owner>/<repo>`의 PR #<M>(`step<k>/<slug>` → `main`, 마일스톤 메인 이슈 #<N>)을 **적대적으로**
> 리뷰해 줘. 이 코드는 틀렸다고 가정하고, 틀린 곳을 증명하는 것이 목표다. 너는 코드를 고치거나 커밋하지 않는다.
> 작업 트리는 이미 PR head에 있다. [맡은 경로: <경로들>]
>
> 1. `gh pr view <M>`, `gh pr diff <M>`, `gh issue view <N> --comments`, 메인 이슈가 링크한 로드맵의 R<k>
>    (claude.ai 아티팩트 링크는 Artifact 도구의 `read`로)를 읽는다.
> 2. `cmake --preset debug`, `cmake --build --preset debug --parallel`, `ctest --preset debug`로 빌드·테스트한다.
> 3. 다음을 찾는다. 가능하면 작은 입력이나 테스트를 직접 만들어 돌려서 확인하고, 임시로 바꾼 것은 되돌린다.
>    - aapt2와 다른 결과: android17-release 소스의 해당 부분과 비교한다. 비교한 함수 이름을 근거로 적는다.
>    - 경계 입력: 빈 값, 최대·최소, 잘못된 UTF-8, 이상한 XML, 아주 큰 파일, 중복 정의, 파일·폴더 순서.
>    - 플랫폼 차이: Windows 경로·UTF-16, 로케일(소수점 쉼표), unsigned char(Linux arm64), 엔디언, 32/64비트.
>    - 오류 처리: 삼키는 오류, 예외가 API 밖으로 새는 곳, 진단 메시지가 aapt2와 다른 곳.
>    - API·ABI: 공개 헤더가 구현 세부를 드러내는 곳, 앞으로 바꾸기 어려운 모양.
>    - 테스트가 통과해도 잡지 못하는 경우, golden이 확인하지 않는 부분.
>    - 코드와 어긋난 주석·README·로드맵.
>    취향 문제는 넣지 않는다.
> 4. 각 지적마다 `gh issue list --state all --search "<핵심 단어>"`로 기존 이슈를 찾고, 같은 문제를 다루는
>    열린 이슈가 있으면 **중복 #X**로 표시한다.
> 5. 리뷰를 PR에 한 번에 올린다: `gh api repos/<owner>/<repo>/pulls/<M>/reviews --method POST --input <json>`,
>    `event: "COMMENT"`, `commit_id`는 PR head SHA. `body`는 `## 적대적 리뷰`, `### 확인한 것`,
>    `### 정리` 표(# / 항목 / 심각도). `comments`는 지적 하나당 인라인 코멘트 하나
>    (`**<번호>. <요약> (<심각도>)**`, 근거, `**제안**`). diff 밖의 줄은 body에만 쓴다.
> 6. 마지막에 지적 목록을 이 형식으로 돌려줘:
>    `번호 | 성격(버그 / 정리·성능 / 테스트 / 문서 / 결정 필요) | 심각도 | 파일:줄 | 한 줄 요약 | 근거 | 제안 | 중복 #X 또는 -`.

## 7. 서브이슈 등록

리뷰어가 돌려준 지적(중복 제외)과 4단계의 **추가 이슈 목록**을 공용 절차 **서브이슈 등록**으로 메인 이슈
`<N>`의 서브이슈로 등록한다. 출처는 `PR #<M> 리뷰 <번호>번에서 나온 항목입니다.`(병렬 리뷰면 번호 앞에
리뷰어 구분을 붙인다) 또는 `#<N> 작업 중 발견한 항목입니다.` 지적끼리 의존하면 `--blocked-by`를 붙인다.
여기서는 코드를 고치지 않는다. 고치는 것은 `/fix-issue`가 서브이슈마다 한다.

PR에 코멘트를 하나 단다(`gh pr comment <M>`): `## 리뷰 → 서브이슈`, 지적 번호별로 등록한 서브이슈 또는
중복 #X, PR attribution 줄.

## 8. 처리 순서와 마무리

1. 공용 절차 **처리 순서**로 메인 이슈 `<N>`의 열린 서브이슈 전체(새로 등록한 것 포함) 순서를 표로 보여 준다.
2. 보고: 단계 브랜치, PR 링크, full CI 결과(16개), 등록한 서브이슈 표, 중복으로 넘긴 지적, Development 연결 여부.
3. PushNotification으로 한 줄 알린다. 예: `R5 열림 (PR #120), 서브이슈 9개 등록`.
