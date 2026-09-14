# Next PR Tasks

다음 PR에서 수정해야 할 사항들을 정리한 문서입니다.

## 🔴 High Priority

### 1. util.cpp - search_file() 보안 개선 — 종결 (2026-09)

**상태:** 종결 — 개선이 아니라 함수 자체를 삭제했다 (이슈 #114).

`json_parser()` 가 디렉터리 탐색을 그만두고 `PIM_RUNTIME_JSON_FILE` 경로를 그대로
읽게 되면서(PR #111) 마지막 호출부가 사라졌고, 테스트 스텁도 함께 없어졌다. 이후
`util.cpp` 의 정의와 `util.h` 의 선언을 삭제했다. 그 함수만 쓰던 `sys_newfstatat()`
와 `fcntl.h` / `sys/syscall.h` / `dirent.h` 도 같이 정리했다.

여기 있던 `popen()` 기반 코드와 옵션 1/2 비교, 테스트 계획은 대상이 없어져 제거한다.
이 항목은 태생부터 낡았던 것이 아니다 — 측정된 타임라인은 `docs/SECURITY-NOTES.md`
의 같은 항목에 표로 있다. 설정 파싱 회귀는 `./test/run-parser-config-test.sh` 가 덮는다.

---

## 🟡 Medium Priority

### 2. .github/README.md 모델명 업데이트

**파일:** `.github/README.md:52`

**현재:**
```markdown
- Uses Gemini 2.0 Flash (Experimental) model
```

**수정 필요:**
- 문서와 실제 코드 일치 확인
- `gemini-2.0-flash-exp` 모델명 정확히 명시
- 모델 선택 이유 추가 설명

**제안:**
```markdown
- Uses Gemini 2.0 Flash Experimental (`gemini-2.0-flash-exp`) model
- Fast response times with strong code analysis capabilities
- Free tier available with generous quotas
```

### 3. GitHub Actions 워크플로우 통합 개선

**검토 필요 워크플로우:**
- `gemini-dispatch.yml`
- `gemini-invoke.yml`
- `gemini-review.yml`
- `gemini-triage.yml`
- `shellcheck.yml`
- `build-test.yml`

**확인 사항:**
1. 모든 워크플로우가 최신 모델 버전 사용하는지
2. 중복된 기능 통합 가능성
3. 비용 최적화 (불필요한 실행 제거)
4. 에러 처리 개선

---

## 🟢 Low Priority / Future

### 4. AES 암호화 마이그레이션 준비

**상태:** 장기 계획 (Major Version에서 진행)

**필요 작업:**
1. 키 버전 관리 시스템 설계
2. 마이그레이션 도구 개발
3. 하위 호환성 테스트
4. 고객 마이그레이션 가이드 작성

**참고:** `docs/SECURITY-NOTES.md` 참조

### 5. 코드 스타일 통일

**검토 항목:**
- 일관된 들여쓰기 (탭 vs 스페이스)
- 로깅 형식 통일
- 에러 처리 패턴 표준화
- 주석 스타일 가이드

### 6. 단위 테스트 추가

**우선순위 테스트 대상:**
1. AES 암호화/복호화 함수
2. 파서 함수들 — `test/test_parser_config.cpp` 가 일부를 덮는다
3. IPC 통신 함수들

(`search_file()` 은 삭제돼 대상에서 제외했다 — 위 1번 항목 참조.)

---

## 📋 작업 순서 권장

1. **First PR (문서 정비):**
   - .github/README.md 업데이트
   - 워크플로우 파일 검토 및 개선

2. **Second PR (품질 개선):**
   - 코드 스타일 통일
   - 단위 테스트 추가

3. **Future (장기 계획):**
   - AES 마이그레이션 준비

---

## 📚 참고 자료

- GLib Documentation: https://docs.gtk.org/glib/
- OWASP Command Injection: https://owasp.org/www-community/attacks/Command_Injection
- GitHub Actions Best Practices: https://docs.github.com/actions/security-guides/security-hardening-for-github-actions

---

**작성일:** 2026-01-20
**마지막 업데이트:** 2026-09-14
**담당자:** [Your Name]
**리뷰어:** [Reviewer Name]
