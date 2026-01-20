# Next PR Tasks

다음 PR에서 수정해야 할 사항들을 정리한 문서입니다.

## 🔴 High Priority

### 1. util.cpp - search_file() 보안 개선

**파일:** `util.cpp:360-388`

**현재 문제:**
- `popen()`을 사용한 쉘 명령어 실행
- 커맨드 인젝션 가능성 (입력 검증 없음)
- 성능 저하 (매 호출마다 5개 프로세스 생성)

**현재 코드:**
```cpp
sprintf(str, "ls -ptr %s/%s*%s 2>/dev/null | grep -v '/$' | grep '\\%s$' | tail -1 | tr -d '\r\n'",
        path, prefix, suffix, suffix);
fp = popen(str, "r");
```

**권장 해결 방법:**

#### 옵션 1: GLib 기반 구현으로 복원 (권장)
```cpp
gchar *search_file(const gchar* path, const gchar* prefix, const gchar* suffix)
{
    static gchar str[128];
    GDir *dir = NULL;
    GError *error = NULL;
    const gchar *filename = NULL;
    gchar *latest_file = NULL;
    time_t latest_mtime = 0;

    str[0] = '\0';

    // GLib의 GDir API를 사용한 안전한 파일 검색
    dir = g_dir_open(path, 0, &error);
    if (error != NULL) {
        __LOG(LOG_ERR, "[CFG][%s:%d] g_dir_open 실패: %s", _FILE_, __LINE__, error->message);
        g_error_free(error);
        return str;
    }

    // prefix와 suffix가 일치하는 가장 최근 파일 찾기
    while ((filename = g_dir_read_name(dir)) != NULL) {
        if (g_str_has_prefix(filename, prefix) && g_str_has_suffix(filename, suffix)) {
            gchar *fullpath = g_build_filename(path, filename, NULL);
            GStatBuf st;

            if (g_stat(fullpath, &st) == 0 && S_ISREG(st.st_mode)) {
                if (st.st_mtime > latest_mtime) {
                    latest_mtime = st.st_mtime;
                    g_free(latest_file);
                    latest_file = g_strdup(fullpath);
                }
            }
            g_free(fullpath);
        }
    }
    g_dir_close(dir);

    if (latest_file != NULL) {
        g_strlcpy(str, latest_file, sizeof(str));
        __LOG(LOG_INFO, "[CFG][%s:%d] search_file 찾음: %s", _FILE_, __LINE__, str);
        g_free(latest_file);
    } else {
        __LOG(LOG_WARNING, "[CFG][%s:%d] 파일을 찾지 못함: %s/%s*%s",
              _FILE_, __LINE__, path, prefix, suffix);
    }

    return str;
}
```

**장점:**
- ✅ 쉘 인젝션 불가능
- ✅ 빠른 실행 (프로세스 생성 없음)
- ✅ glibc 버전 독립적 (`g_stat` 사용)
- ✅ 명확한 에러 처리
- ✅ 메모리 안전성 (GLib 메모리 관리)

#### 옵션 2: 현재 구현 유지 + 입력 검증 추가 (차선책)
```cpp
gchar *search_file(const gchar* path, const gchar* prefix, const gchar* suffix)
{
    FILE *fp;
    static gchar str[128];

    // 입력 검증: 위험한 문자 차단
    if (strchr(path, ';') || strchr(path, '|') || strchr(path, '&') ||
        strchr(path, '`') || strchr(path, '$') || strchr(path, '>') ||
        strchr(prefix, ';') || strchr(prefix, '|') || strchr(prefix, '&') ||
        strchr(suffix, ';') || strchr(suffix, '|') || strchr(suffix, '&')) {
        __LOG(LOG_ERR, "[CFG][%s:%d] 유효하지 않은 문자 감지", _FILE_, __LINE__);
        str[0] = '\0';
        return str;
    }

    // 경로 정규화 검증
    gchar *real_path = realpath(path, NULL);
    if (real_path == NULL) {
        __LOG(LOG_ERR, "[CFG][%s:%d] 유효하지 않은 경로: %s", _FILE_, __LINE__, path);
        str[0] = '\0';
        return str;
    }
    free(real_path);

    sprintf(str, "ls -ptr %s/%s*%s 2>/dev/null | grep -v '/$' | grep '\\%s$' | tail -1 | tr -d '\r\n'",
            path, prefix, suffix, suffix);

    fp = popen(str, "r");
    if (NULL == fp) {
        perror("popen() fail");
        __LOG(LOG_CRIT, "[CFG][%s:%d] popen 실패", _FILE_, __LINE__);
        str[0] = '\0';
        return str;
    }

    str[0] = '\0';
    if (fgets(str, sizeof(str), fp) != NULL) {
        gsize len = strlen(str);
        if (len > 0 && str[len-1] == '\n') {
            str[len-1] = '\0';
        }
    }

    int status = pclose(fp);
    if (status != 0) {
        __LOG(LOG_WARNING, "[CFG][%s:%d] pclose 반환: %d", _FILE_, __LINE__, status);
    }

    __LOG(LOG_INFO, "[CFG][%s:%d] search_file: %s", _FILE_, __LINE__, str);
    return str;
}
```

**테스트 계획:**
```bash
# 단위 테스트 작성
# 1. 정상 파일 검색
# 2. 존재하지 않는 경로
# 3. 빈 디렉토리
# 4. 여러 매칭 파일 (최신 파일 선택 확인)
# 5. 특수 문자가 포함된 파일명
# 6. (옵션 2의 경우) 악의적 입력 시도
```

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
1. `search_file()` - 다양한 입력 케이스
2. AES 암호화/복호화 함수
3. 파서 함수들
4. IPC 통신 함수들

---

## 📋 작업 순서 권장

1. **First PR (긴급):**
   - util.cpp `search_file()` 보안 개선 (옵션 1 권장)
   - 테스트 케이스 작성

2. **Second PR (문서 정비):**
   - .github/README.md 업데이트
   - 워크플로우 파일 검토 및 개선

3. **Third PR (품질 개선):**
   - 코드 스타일 통일
   - 단위 테스트 추가

4. **Future (장기 계획):**
   - AES 마이그레이션 준비

---

## 🔍 검증 방법

### search_file() 수정 후 테스트
```bash
# 1. 빌드 확인
make clean && make

# 2. 기존 기능 동작 확인
./bin/your_app

# 3. 메모리 누수 검사 (GLib 버전)
valgrind --leak-check=full ./bin/your_app

# 4. 성능 비교
# before: popen 버전
# after: GLib 버전
time ./test_search_file_performance

# 5. Static Analysis
cppcheck --enable=all util.cpp
```

### GitHub Actions 워크플로우 테스트
```bash
# 로컬에서 act를 사용한 워크플로우 테스트
act pull_request -W .github/workflows/gemini-auto-review.yml
```

---

## 📚 참고 자료

- GLib Documentation: https://docs.gtk.org/glib/
- OWASP Command Injection: https://owasp.org/www-community/attacks/Command_Injection
- GitHub Actions Best Practices: https://docs.github.com/actions/security-guides/security-hardening-for-github-actions

---

**작성일:** 2026-01-20
**마지막 업데이트:** 2026-01-20
**담당자:** [Your Name]
**리뷰어:** [Reviewer Name]
