# 채널 시작 진단 이력 문서 정리 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 오래된 미추적 handoff를 현재 코드와 고정 커밋에 근거한 한국어 장애 이력 문서로 변환하고 기존 검증 문서의 참조를 갱신한다.

**Architecture:** 하나의 역사 문서가 수정 전 관측, 해석, 구현 결과와 한계를 담당한다. 기존 PR #38 검증 문서는 그 이력 문서의 존재만 참조하며 시험 결과 본문은 변경하지 않는다.

**Tech Stack:** Markdown, Git, `grep`, `git show`, `git merge-base`

## Global Constraints

- 제품 코드, 설정, 빌드 파일과 타깃 상태를 변경하지 않는다.
- 모든 shell command는 `rtk`를 앞에 붙여 실행한다.
- 문서는 한국어로 작성하고 비밀번호, 토큰, 개인별 절대 checkout 경로를 포함하지 않는다.
- `.jhw/`, `.superpowers/` 실행 산출물과 `tmp/` 로그를 새로 추적하지 않는다.
- 과거 사실은 고정 Git SHA로, 현재 동작은 현재 소스 위치로 근거를 남긴다.
- 수정 후 원래 장애를 같은 조건에서 재현하지 않았다면 해소를 실증했다고 표현하지 않는다.

---

### Task 1: 수정 전 handoff를 추적 가능한 장애 이력으로 변환

**Files:**
- Create: `docs/CHANNEL_STARTUP_DIAGNOSTICS_HISTORY.md`
- Delete: `docs/gstapp-handoff-2026-08-11-channel-logging.md`
- Modify: `docs/PR38_REVIEW_FOLLOWUP_VALIDATION_20260814.md:213-214`

**Interfaces:**
- Consumes: 설계 문서의 기준 SHA와 원본 handoff의 2026-08-11 원시 로그
- Produces: 현재 지침과 구별되는 고정된 역사 기록 및 기존 검증 문서의 추적 문서 참조

- [ ] **Step 1: 수정 전 문서가 현재 기준으로 실패하는 지점을 확인한다**

Run:

```bash
rtk grep -nE '동작은 정상이고 로그가 문제|제안:|추가 검토|커밋되지 않은 변경 5개|dist/pim/opt/pim/bin/gstApp|/home/jhw/' docs/gstapp-handoff-2026-08-11-channel-logging.md
```

Expected: 현재형 단정, 미완료 제안, 오래된 작업 트리 경고, 잘못된 배포 경로와 개인별 절대 경로가 검출된다.

- [ ] **Step 2: 문서 근거가 되는 커밋과 현재 구현을 다시 확인한다**

Run:

```bash
rtk git merge-base --is-ancestor 29bf631 69db31a
rtk git merge-base --is-ancestor 7977cda 69db31a
rtk git merge-base --is-ancestor 3596edc 69db31a
rtk git show --stat --oneline 7977cda
rtk git show --stat --oneline 3596edc
rtk git -C ../pim-package-jhw show --stat --oneline 7b2a112
rtk git -C ../pim-package-jhw show --stat --oneline 7802a08
rtk git -C ../pim-package-jhw show --stat --oneline 6e28828
rtk git -C ../pim-package-jhw show --stat --oneline 63d8484
rtk grep -nE 'NO DATA|link_status|link_status_recheck|start_video_time_chk' encoderBin.cpp main.cpp muxSinkBin.cpp
rtk grep -n 'dist/pim/usr/local/bin' update_bin.sh
```

Expected: 모든 명령이 성공하고 해결 커밋이 병합 기준점의 ancestor이며 현재 구현과 배포 대상이 설계 내용과 일치한다.

- [ ] **Step 3: 원본을 역사 문서로 다시 작성한다**

`docs/CHANNEL_STARTUP_DIAGNOSTICS_HISTORY.md`를 다음 순서로 작성한다.

1. 제목 아래에 “2026-08-11 수정 전 장애를 보존한 역사 기록이며 현재 운용 지침이 아님”을 명시한다.
2. 기준점 표에 수정 전 `29bf631`, 해결 `7977cda`·`3596edc`, 병합 `69db31a`를 기록한다.
3. 원본의 06:31:58~06:32:31 로그 다섯 줄을 원시 관측으로 보존한다.
4. `GST_MESSAGE_ERROR` 로그가 없었다는 것은 해당 handler가 실행되지 않았다는 관측이며 하드웨어·드라이버 정상의 증명이 아니라는 해석 한계를 쓴다.
5. 수정 전 진단 공백을 채널별 무데이터 경고 부재, 링크 상태 미확인/단절 구분 부재, 마커의 대표 채널 의존성으로 정리한다.
6. 구현 결과 표에 `encoderBin.cpp`의 채널별 `NO DATA`, `main.cpp`의 3상태 링크 판정과 PLAYING 후 재검사, `muxSinkBin.cpp`의 세션 기반 마커를 연결한다.
7. `pim-package-jhw` 결과를 `7b2a112`, `7802a08`, `6e28828`, `63d8484`로 고정한다.
8. 검증 기준은 “프로세스를 죽이지 않는가”가 아니라 로그만으로 문제 채널과 판정 단계를 식별할 수 있는가임을 보존한다.
9. 원래 장애를 수정 후 동일 조건으로 재현한 원시 증거는 이 문서에 없다는 한계를 명시한다.
10. 배포 경로는 `pim-package-jhw/dist/pim/usr/local/bin/gstApp`로 기록한다.

원본 파일은 새 문서 작성 후 삭제하여 같은 내용의 현재형 handoff가 함께 남지 않게 한다.

- [ ] **Step 4: 기존 검증 문서의 참조를 갱신한다**

`docs/PR38_REVIEW_FOLLOWUP_VALIDATION_20260814.md:213-214`의 “미추적 파일을 그대로 보존했다”는 문장을 다음 의미로 바꾼다.

```markdown
- 당시 채널 미기동 진단 handoff는 현재 동작과 구분되는 역사 기록으로 정리해
  `docs/CHANNEL_STARTUP_DIAGNOSTICS_HISTORY.md`에서 추적한다.
```

- [ ] **Step 5: stale 표현, 자격 증명과 경로 누출이 없는지 검증한다**

Run:

```bash
rtk test ! -e docs/gstapp-handoff-2026-08-11-channel-logging.md
rtk grep -nE '현재 운용 지침이 아님|29bf631|7977cda|3596edc|69db31a|7b2a112|7802a08|6e28828|63d8484|동일 조건.*재현' docs/CHANNEL_STARTUP_DIAGNOSTICS_HISTORY.md
! rtk grep -nE '제안:|추가 검토|커밋되지 않은 변경 5개|dist/pim/opt/pim/bin/gstApp|/home/jhw/' docs/CHANNEL_STARTUP_DIAGNOSTICS_HISTORY.md
! rtk grep -nEi '(password|passwd|token|secret|api[_-]?key)[[:space:]]*[:=][[:space:]]*[^[:space:]`"]+' docs/CHANNEL_STARTUP_DIAGNOSTICS_HISTORY.md
rtk grep -n 'CHANNEL_STARTUP_DIAGNOSTICS_HISTORY.md' docs/PR38_REVIEW_FOLLOWUP_VALIDATION_20260814.md
```

Expected: 새 문서의 필수 근거와 한계가 모두 존재하고 금지 표현·경로·자격 증명 패턴은 검출되지 않으며 기존 검증 문서가 새 파일을 참조한다.

- [ ] **Step 6: 문서 전용 변경과 형식을 검증한다**

Run:

```bash
rtk git diff --check
rtk git status --short
rtk git diff --stat 91f3718
rtk git diff --name-only 91f3718
```

Expected: 추적 변경 파일은 새 이력 문서, PR #38 검증 문서와 이 계획 문서뿐이고,
미추적이던 이전 handoff는 작업 트리에 남지 않으며 whitespace 오류가 없다.

- [ ] **Step 7: 독립 읽기 전용 리뷰를 수행한다**

리뷰어는 원본 handoff, 두 저장소의 고정 SHA, 현재 세 소스 파일과 새 문서를 대조한다. Critical 또는 Important 정확성 문제, 현재형 오해 가능성, 자격 증명 노출이 발견되면 수정 후 Step 5~7을 반복한다.

- [ ] **Step 8: 검증된 문서 변경을 커밋한다**

Run:

```bash
rtk git add docs/CHANNEL_STARTUP_DIAGNOSTICS_HISTORY.md docs/PR38_REVIEW_FOLLOWUP_VALIDATION_20260814.md docs/superpowers/plans/2026-08-14-channel-startup-diagnostics-history.md
rtk git diff --cached --check
rtk git commit -m "docs: preserve channel startup diagnostics history"
```

Expected: 문서 파일만 포함된 새 커밋이 생성되고 제품 코드와 타깃 상태는 변경되지 않는다.
