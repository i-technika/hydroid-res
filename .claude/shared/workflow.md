# /fix-issue, /open-ms, /close-ms가 같이 쓰는 절차

스킬 본문이 "공용 절차 X"라고 하면 여기의 그 절을 따른다. 헬퍼는 `python .claude/shared/hydroid_gh.py`
(아래에서 `hgh`)이고 결과는 JSON이다.

## 저장소의 구조

- 마일스톤 `R<k>: ...`마다 **메인 이슈**가 하나 있다(제목 `R<k>: ...`, Type `Task`, label `enhancement`,
  parent 없음, 예: #96). 본문은 [설계 및 개발 로드맵](https://claude.ai/artifact/8T6XejqMo8TRPPcT42UZfF)의
  해당 단계를 가리킨다.
- 마일스톤의 코드는 `main`에서 만든 `step<k>/<영문-slug>` 브랜치에 쌓이고(예: `step4/resource-table`),
  그 브랜치 → `main` PR(예: #77, 제목 `<k>단계: ...`)이 메인 이슈를 닫는다.
- 나머지 이슈는 메인 이슈의 **서브이슈**다. 서브이슈 PR은 `fix/<N>-<slug>` → `step<k>/...`이다.
- 프로젝트는 `Hydroid Project`(사용자 프로젝트 3번). 필드: `Status`(Todo / In Progress / Done), `단계`(R1~),
  `영역`, `플랫폼`, `res 릴리스`. Status는 프로젝트 자동화가 대부분 바꾼다.
- 담당자는 항상 `i-technika`.
- `hgh kind <N>`의 `kind`가 `main` / `sub` / `other`를 알려 준다.

## CI 범위

`.github/workflows/ci.yml`이 이벤트에 따라 범위를 고른다.

| 범위 | 언제 | job |
| --- | --- | --- |
| quick (4) | `main`이 아닌 브랜치로 가는 PR (서브이슈 PR) | `core (ubuntu-24.04)`, `core (ubuntu-24.04, shared)`, `core (Clang, ASan and UBSan)`, `clang-format` |
| full (15) | `main`으로 가는 PR, `main` push, `gh workflow run ci.yml -f scope=full` | 아래 전부 |

full의 15개: 기존 11개 — `core (ubuntu-24.04 / macos-15 / windows-2022)`, 같은 세 OS의 `, shared`,
`core (CMake 3.24)`, `core (installed dependencies)`, `core (Clang, ASan and UBSan)`, `clang-format`,
`golden app builds` — 에 더해 `core (ubuntu-24.04, release)`, `core (windows-2022, release)`(최적화가 내는
경고와 NDEBUG), `core (ubuntu-24.04-arm)`(char가 unsigned인 유일한 러너), `locale tables`
(`scripts/gen_locale_data.py`로 `src/core/LocaleDataTables.inc`를 다시 만들어 비교. android.googlesource.com에서
내려받으므로 네트워크 문제로 실패할 수 있다).

full 실행 결과를 확인할 때는 job 이름과 수가 위 목록과 맞는지 본다(빠진 job이 있으면 범위가 quick으로
돈 것이다):
```
gh run view <run id> --json jobs --jq '.jobs[] | "\(.name)\t\(.conclusion)"'
```
CI는 오래 걸리므로 `gh pr checks <M> --watch` 또는 `gh run watch <run id> --exit-status`를 백그라운드로
돌리고 끝날 때 알림을 받는다. 실패하면 `gh run view <id> --log-failed`로 원인을 본다.

## 서브이슈 등록

작업이나 리뷰에서 나온 문제를 메인 이슈 `<P>`의 서브이슈로 등록한다. 개발자에게 묻지 않고 등록한다.

1. **중복 확인**: 핵심 단어로 `gh issue list --state all --search "<단어>" --json number,title,state`를 찾는다.
   같은 문제를 다루는 열린 이슈가 있으면 등록하지 않고 그 이슈에 근거를 코멘트로 보탠다(새 근거가 있을 때만).
2. **제목**(한국어): 무엇이 어떻게 잘못되는지와 그 결과를 한 줄로. `<무엇이 문제>: <결과나 aapt2와의 차이>`.
   예: `ResScanner가 폴더 이름순으로 파일을 읽음: aapt2의 전체 경로순과 달라 values-land/보다 values/를 먼저 처리`
3. **본문**(한국어, scratchpad 파일에 쓴다):
   - `## 문제`: 근거를 `파일:줄`과 함께. 재현했으면 어떻게, 코드로만 확인했으면 그렇다고 적는다.
   - `## 제안`: 고치는 방법. 여럿이면 `- ` 목록.
   - `## 확인할 것`(있으면): 넣을 테스트, golden.
   - 마지막 줄에 출처: `PR #<M> 리뷰 <번호>번에서 나온 항목입니다.` 또는 `#<N> 작업 중 발견한 항목입니다.`
4. **Type과 label**:

   | 성격 | Type | label |
   | --- | --- | --- |
   | 동작이 틀림, aapt2와 다른 결과, 예외·크래시 | `Bug` | `bug` |
   | 정리, 중복 제거, 성능, 테스트 보강, 빌드·CI | `Task` | `enhancement` |
   | 새 기능, 아직 없는 API | `Feature` | `enhancement` |
   | 문서만 | `Task` | `documentation` |
   | 개발자가 정해야 하는 것 | `Task` | `question` (필요하면 위 label과 함께) |

5. **만들기**: 마일스톤은 메인 이슈의 것, parent는 메인 이슈다.
   ```
   gh issue create --title "<제목>" --body-file <파일> --type <Type> --label <label> \
     --milestone "<메인 이슈의 마일스톤 제목>" --parent <P> --project "Hydroid Project" \
     [--blocked-by <먼저 끝나야 하는 열린 이슈 번호들>]
   ```
   다른 열린 이슈가 먼저 끝나야 고칠 수 있으면 `--blocked-by`를 붙인다(처리 순서에 쓰인다).
6. **프로젝트 필드**: `hgh copy-fields <P> <새 번호> --status Todo` (단계·영역·플랫폼·res 릴리스를 메인 이슈와 같게).
7. **확인**: `hgh kind <새 번호>`가 `kind: "sub"`, parent `<P>`, 마일스톤이 맞는지 본다.

등록한 것은 `# / Type / label / 제목 / 출처` 표로 보고한다.

## 처리 순서

`hgh subs <P>`로 열린 서브이슈를 모두 가져와(새로 등록한 것 포함) 처리할 순서를 정한다. 기준은 위에서부터:

1. `blockedBy`, 또는 본문의 "#X 이후", "#X의 결과를 보고" 같은 의존: X 뒤로.
2. `question` label(결정이 필요한 것): 결정을 기다리는 동안 막히는 이슈 앞에 두고 "결정 필요"로 표시.
3. 같은 파일·함수를 고치는 이슈는 이어서 처리한다. 그 안에서는 아래 층(데이터 구조, 파서, 스캐너)을
   먼저 고쳐 위 층의 수정이 그 결과 위에 서게 한다. 충돌과 golden 재생성이 줄어든다.
4. 출력이 바뀌는 `Bug`(golden이 바뀜)를 동작을 그대로 두는 `Task`(정리, 성능)보다 먼저.
5. 나머지는 영향이 큰 것 먼저.

보여 줄 형식(한국어):

| 순서 | # | Type | 제목 | 이유 |
| --- | --- | --- | --- | --- |
| 1 | #80 | Bug | ... | #81·#88과 같은 `ResScanner` 순서를 쓰므로 먼저 |

새로 등록한 이슈는 번호 옆에 `(새)`를 붙인다. 표 아래에 "다음: `/fix-issue <1번 이슈>`"를 적는다.
