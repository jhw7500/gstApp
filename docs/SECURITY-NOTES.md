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

### 3. Config Type Confusion (parser.cpp) — RESOLVED, with a documented residual

**Status:** RESOLVED (2026-09, issue #116) — the type-confused accessor was
split per destination type and then deleted.

**Location:** `parser.cpp` — `json_object_get_value()` and
`json_sub_object_get_value()` (both removed), replaced by `json_get_string()`
and `json_get_bool()`.

**What was wrong.**
`json_object_get_value()` branched on the JSON *value's* type and cast the
caller's `gpointer data` to match it. The caller's expected type never reached
the function, so a wrong-typed config value wrote the wrong width to the
destination. Measured on an isolated copy during the PR #115 tribunal:

| config value | destination | result |
|---|---|---|
| `"vhl_name": 1234567890000000` | `const gchar *ohtName` (8 bytes) | 4-byte write; the low half became a saturated int and the high half kept the old pointer — a wild pointer |
| `"ae_on": "yes"` | `gboolean ae_on` (4 bytes) | 8-byte pointer written; the adjacent `ae_gain` took the pointer's high half |
| `"ae_on": [11,22,33,44]` | `gboolean ae_on` (4 bytes) | one write per array element, past the field; the length is chosen by the config file |

`json_parser()` returned 0 in each case, so gstApp started on the corrupted
configuration.

**Resolution.** Each destination type has its own accessor, so the written width
is fixed by the function that writes it rather than by the file being read. A
type mismatch leaves `*out` at the caller's default, logs `LOG_ERR` and
increments the parse-error counter. Reproduce with:

```
./test/run-parser-config-test.sh
```

The change-detecting cases are `test_wrong_typed_string_leaves_destination_untouched`,
`test_wrong_typed_bool_leaves_destination_untouched` and
`test_array_in_scalar_slot_leaves_destination_untouched` in
`test/test_parser_config.cpp`; `test_integer_zero_one_still_accepted_for_bool`
is a preservation check, since `gboolean` is `gint` and integer `0`/`1` was
already width-correct.

**Residual gap — a wrong type is now safe, but still not fatal.**
Issue #116's acceptance condition (a) stated that counting the mismatch in
`g_cfg_errors` would stop `json_parser()` from returning 0. The code does not
behave that way. Check with:

```
grep -n 'g_cfg_errors' parser.cpp
sed -n '/^gint ParserClass::check_arg/,/^}/p' parser.cpp | grep -n 'return'
```

`g_cfg_errors` is read in one place, and only to emit a summary `LOG_ERR`.
`check_arg()` does return -1 — on recording duration, on an unsupported
resolution and on the total-fps limit — but none of those paths consults
`g_cfg_errors`. So a wrong-typed value
no longer corrupts memory, but gstApp still starts, using the default for that
field. That is the policy every other config accessor in this file already
follows (`json_get_int()`, `json_get_int_array()`,
`json_object_get_bool_optional()`): bad value, keep default, log, continue.
Making type errors fatal would also change startup behaviour for those
pre-existing paths, so it is deliberately outside this change.

**Trigger.** The input is `/run/pim-camera/config/pim_runtime.json`, written by
`camera_runtime_config.py` in the separate `pim-package-jhw` repository. The
trigger is a producer bug, a hand-edited file, or a partially written file —
not an external attacker. The PR #115 reviewer rated it MEDIUM for that reason.

---

## Security Review History

| Date       | Reviewer | Scope                    | Critical Findings |
|------------|----------|--------------------------|-------------------|
| 2026-01-20 | Claude   | Commit diff analysis     | 2 (1 deferred)    |
| 2026-09-14 | PR #115 tribunal reviewer A | parser.cpp config accessors | 1 MEDIUM (issue #116, resolved above) |

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
