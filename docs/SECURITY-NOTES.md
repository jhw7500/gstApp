# Security Notes

## Known Security Issues and Deferral Rationale

### 1. AES Hardcoded Key (aes.cpp)

**Status:** ⚠️ DEFERRED (Not fixed in current release)

**Location:**
- `aes.cpp:262` - `encrypt_get_passwd()`
- `aes.cpp:308` - `encrypt_change_passwd()`

**Issue:**
The AES encryption functions use a hardcoded NIST test vector as the encryption key:
```cpp
BYTE Key[] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
              0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
```

This is a publicly known test vector from FIPS-197 (AES specification), making encrypted passwords vulnerable to decryption by anyone with access to the encrypted file.

**Security Impact:**
- **Severity:** Critical
- **Attack Vector:** Local file access
- **Affected Functions:** Password storage and password change operations
- **Exploitability:** High (known key value)

**Why Deferred:**
1. **Backward Compatibility:** Deployed production devices already use this encryption key
2. **User Impact:** Changing the key would invalidate all existing stored passwords
3. **Migration Complexity:** No automated migration path exists for field-deployed units
4. **Customer Disruption:** Users would lose access and require manual password reset

**Deferral Decision Date:** 2026-01-20

**Mitigation in Place:**
- Password files should have restrictive permissions (600 or 640)
- Physical access control to deployed devices
- Network isolation of management interfaces

**Recommended Fix (For Future Major Version):**
```cpp
// Option 1: Load key from secure storage
int load_encryption_key(const char *keyfile, BYTE *key, size_t keylen);

// Option 2: Derive key from device-unique identifier
int derive_device_key(BYTE *key, size_t keylen);

// Option 3: Use TPM/HSM for key storage
int get_tpm_key(BYTE *key, size_t keylen);

// Migration path required:
// 1. Detect old encryption format
// 2. Decrypt with old key
// 3. Re-encrypt with new key
// 4. Mark as migrated
```

**Migration Strategy (Future):**
1. Implement key versioning in password file format
2. Support both old and new keys during transition period
3. Auto-migrate on first password change
4. Provide manual migration tool for field deployment
5. Deprecation timeline: 1-2 year overlap period

**References:**
- NIST FIPS-197: https://csrc.nist.gov/publications/detail/fips/197/final
- CWE-321: Use of Hard-coded Cryptographic Key
- OWASP: Cryptographic Storage Cheat Sheet

---

### 2. Legacy Command Injection Risk (util.cpp) — RESOLVED

**Status:** RESOLVED (2026-09) — the function was deleted.

**Location:** `util.cpp` / `util.h` - `search_file()` (removed)

**Resolution:**
`search_file()` and its `util.h` declaration are gone. Its last caller
disappeared when `json_parser()` stopped scanning a directory and began reading
the exact path `PIM_RUNTIME_JSON_FILE` (PR #111); the test stub went with it.
`git grep 'search_file' -- '*.cpp' '*.h'` now returns nothing; the name survives
only in documentation (this file, `NEXT-PR-TASKS.md`, `RELEASE_NOTES_v1.3.md`
and `docs/superpowers/plans/2026-08-31-edgeconf-array-fatal.md`). No caller, no
code, no residual risk.

**What this entry described, and when it was true.**
The counts below are raw text occurrences inside the function body, measured
with `git show <commit>:util.cpp | awk '/gchar \*search_file/,/^}/' |
grep -c popen`:

| Commit | Date | hits | |
|---|---|---|---|
| `8413fd5^` | | 3 | the shell pipeline: one `popen()` call plus two diagnostic strings |
| `8413fd5` | 2026-01-19 16:23 | 1 | calls replaced by a GLib `GDir` scan; the single hit is a comment |
| `462f792` | 2026-01-20 09:26 | 1 | **this entry authored** — the pipeline it quotes was not in the code at that moment |
| `49f65be` | 2026-01-20 09:31 | 3 | `revert: Defer security improvements` — **pipeline restored**, again one call plus two diagnostic strings |
| `68a6489` | 2026-02-09 13:09 | 0 | removed for good, replaced by `opendir()` / `readdir()` |
| `b96ea98` | 2026-09-14 | 0 | state at deletion |

So the entry was accurate — and the command-injection vector genuinely present
— from `49f65be` until `68a6489`, roughly three weeks. It went stale in 2026-02,
not at birth, and its `util.cpp:360-388` reference was accurate over the same
window (`search_file()` began at line 360 at both `462f792` and `68a6489^`).

---

## Security Review History

| Date       | Reviewer | Scope                    | Critical Findings |
|------------|----------|--------------------------|-------------------|
| 2026-01-20 | Claude   | Commit diff analysis     | 2 (1 deferred)    |

---

## Reporting Security Issues

If you discover a security vulnerability in this codebase:

1. **Do NOT** open a public GitHub issue
2. Contact the security team directly: [security contact]
3. Provide:
   - Detailed description of the vulnerability
   - Steps to reproduce
   - Potential impact assessment
   - Suggested fix (if available)

Response SLA: 48 hours for acknowledgment

---

## Secure Development Guidelines

### For Developers

1. **Never hardcode secrets or keys**
   - Use environment variables
   - Use secure key storage (HSM/TPM)
   - Use key derivation functions

2. **Avoid shell execution**
   - Prefer native library functions
   - Use argument arrays over shell strings
   - Validate all inputs if shell execution is unavoidable

3. **Input validation**
   - Validate at system boundaries
   - Use allowlists over denylists
   - Sanitize before use in sensitive contexts

4. **Cryptography**
   - Use proven libraries (OpenSSL, libsodium)
   - Follow current best practices
   - Regular key rotation where feasible

### For Code Reviewers

- Check for CWE Top 25 vulnerabilities
- Verify input validation at boundaries
- Ensure secrets are not committed
- Review privilege requirements
- Validate error handling

---

**Last Updated:** 2026-01-20
**Next Review Due:** 2026-07-20
