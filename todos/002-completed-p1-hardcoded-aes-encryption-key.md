# 002: Hardcoded AES Encryption Key Exposes All Passwords

## Metadata
- **Status**: ✅ COMPLETED
- **Priority**: p1 (CRITICAL - Security)
- **Issue ID**: 002
- **Tags**: security, code-review, cryptography, critical
- **Dependencies**: None
- **Created**: 2026-01-08
- **Completed**: 2026-01-19
- **Commit**: 8413fd5 "security: 하드코딩된 AES 키 교체 및 popen() 제거"

## Problem Statement

The application uses a **hardcoded AES encryption key** (the NIST AES test vector) to encrypt RTSP passwords. This key is embedded in the source code and is the same across all devices, meaning anyone with access to one device or the source code can decrypt passwords from ALL devices.

### Impact
- **Severity**: CRITICAL
- Complete compromise of RTSP authentication on all deployed devices
- Passwords can be extracted from `/root/shared_v/.passwd` on any device
- Attacker with binary can decrypt all passwords globally
- Violates basic cryptographic security principles

## Findings

### Location
`/home/jhw/ai/claude/projects/gstApp/aes.cpp:262, 317`

### Vulnerable Code
```cpp
// HARDCODED KEY - This is the NIST AES-128 test vector!
BYTE Key[] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
              0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
```

### Additional Issues
- Uses weak ECB mode (patterns leak): aes.cpp:243-257
- Passwords should be hashed, not encrypted
- No per-device key derivation
- Key visible in binary with `strings` command

### Evidence
```bash
# Extract key from binary:
strings bin/gstApp | grep -A1 "2b7e151628aed2a6"

# Decrypt ANY device's password file with this key
# All devices worldwide share the same encryption key!
```

## Proposed Solutions

### Solution 1: Per-Device Key Derivation (RECOMMENDED)
**Description**: Generate unique encryption keys per device using hardware identifiers

**Implementation**:
```cpp
#include <openssl/evp.h>
#include <openssl/kdf.h>

// Derive per-device key from CPU serial + MAC address
bool deriveDeviceKey(uint8_t* output_key, size_t key_len) {
    // Read CPU serial number
    FILE* fp = fopen("/proc/cpuinfo", "r");
    char serial[64] = {0};
    // Parse Serial line...

    // Read MAC address
    char mac[18] = {0};
    // Parse from /sys/class/net/eth0/address

    // Combine and hash with PBKDF2
    char device_id[128];
    snprintf(device_id, sizeof(device_id), "%s:%s:gstApp", serial, mac);

    // Use PBKDF2 with 10,000 iterations
    PKCS5_PBKDF2_HMAC(device_id, strlen(device_id),
                      (unsigned char*)"gstApp-salt-v1", 14,
                      10000, EVP_sha256(),
                      key_len, output_key);
    return true;
}

// Replace AES_ECB_Encrypt/Decrypt to use derived key
```

**Pros**:
- Each device has unique key
- Cannot decrypt other devices' data
- Hardware-bound security
- Backward compatible (can migrate)

**Cons**:
- Requires OpenSSL or similar KDF library
- Must handle key derivation failure
- More complex implementation

**Effort**: Medium (4-6 hours)
**Risk**: Medium (must test thoroughly)

---

### Solution 2: Switch to Password Hashing (RECOMMENDED for passwords)
**Description**: Passwords should be hashed with bcrypt/Argon2, NOT encrypted

**Implementation**:
```cpp
#include <glib.h>

// Use GLib's password hashing (or libsodium if available)
bool storePasswordHash(const char* password, const char* filepath) {
    // Generate salt
    char salt[16];
    g_random_bytes(salt, sizeof(salt));

    // Hash with bcrypt-equivalent
    // (If using libsodium: crypto_pwhash_str)
    char hash[128];
    // pwhash(password, salt) -> hash

    // Store salt + hash
    FILE* fp = fopen(filepath, "w");
    fwrite(salt, 1, sizeof(salt), fp);
    fwrite(hash, 1, sizeof(hash), fp);
    fclose(fp);
    return true;
}

bool verifyPassword(const char* password, const char* filepath) {
    // Read salt + hash
    // Recompute hash with provided password
    // Compare in constant time
    return true;
}
```

**Pros**:
- One-way hash (cannot decrypt)
- Industry standard for password storage
- Resistant to brute force (slow hash)

**Cons**:
- Cannot recover original password
- Requires password verification flow change
- Not backward compatible

**Effort**: Medium (4-6 hours)
**Risk**: High (changes authentication flow)

---

### Solution 3: Use System Keyring (Best Practice)
**Description**: Store secrets in kernel keyring or TPM if available

**Implementation**:
```cpp
#include <keyutils.h>

// Store in kernel keyring (per-session or per-user)
bool storePasswordInKeyring(const char* password) {
    key_serial_t key = add_key("user", "gstApp:rtsp_password",
                                password, strlen(password),
                                KEY_SPEC_USER_KEYRING);
    return key >= 0;
}

const char* getPasswordFromKeyring() {
    key_serial_t key = keyctl_search(KEY_SPEC_USER_KEYRING, "user",
                                      "gstApp:rtsp_password", 0);
    // Read key value
}
```

**Pros**:
- OS-managed security
- Memory-only storage option
- Can use hardware security module

**Cons**:
- Linux-specific
- Requires root or special permissions
- Complex error handling

**Effort**: Large (1-2 days)
**Risk**: High (platform-specific)

---

### Solution 4: IMMEDIATE MITIGATION - Change the Key
**Description**: Replace NIST test vector with random key (temporary fix)

**Implementation**:
```cpp
// Generate with: openssl rand -hex 16
BYTE Key[] = {0xDE, 0xAD, 0xBE, 0xEF, ...};  // Replace with actual random bytes
```

**Pros**:
- 5-minute fix
- Breaks existing exploits
- Buys time for proper solution

**Cons**:
- Still hardcoded (same key on all devices)
- Key still in binary
- Not a real fix

**Effort**: Small (5 minutes)
**Risk**: Low (but provides minimal security improvement)

## Recommended Action

**IMMEDIATE (24 hours)**:
1. **Change hardcoded key** (Solution 4)
   - Replace NIST test vector with random key
   - This breaks publicly-known exploits

**CRITICAL (1 week)**:
2. **Implement per-device key derivation** (Solution 1)
   - Use CPU serial + MAC for unique keys
   - Add key derivation function (PBKDF2)
   - Test on 3+ devices to verify uniqueness

**LONG-TERM (1 month)**:
3. **Switch to password hashing** (Solution 2)
   - Use bcrypt or Argon2 for password storage
   - Update authentication verification code
   - Migrate existing passwords

## Technical Details

### Affected Files
- `/home/jhw/ai/claude/projects/gstApp/aes.cpp` - Hardcoded key definition
- `/home/jhw/ai/claude/projects/gstApp/aes.h` - AES class interface
- `/home/jhw/ai/claude/projects/gstApp/main.cpp:569-585` - Password load/save

### Components Involved
- AESClass::AES_ECB_Encrypt() - Encryption with hardcoded key
- AESClass::AES_ECB_Decrypt() - Decryption with hardcoded key
- AESClass::fileToPass() - Reads encrypted password from disk
- AESClass::passToFile() - Writes encrypted password to disk

### Database/Storage Changes
- Password file: `/root/shared_v/.passwd` (default path)
- Format: 16-byte encrypted password (ECB mode, no IV)
- Migration needed: Re-encrypt with new key derivation

### Related Findings
- Finding 2.2: Weak ECB mode usage (patterns leak)
- Finding 2.3: Should use password hashing instead of encryption
- Finding 6.1: Passwords logged in cleartext (separate issue)

## Acceptance Criteria

- [ ] Hardcoded NIST test vector key replaced
- [ ] Per-device key derivation implemented using CPU serial + MAC
- [ ] Password encryption uses AES-GCM or AES-CBC (not ECB)
- [ ] Unit tests verify different keys on different "devices" (mocked)
- [ ] Integration test: Encrypted password on device A cannot be decrypted on device B
- [ ] Migration script to re-encrypt existing passwords with new key
- [ ] Documentation updated with key derivation algorithm
- [ ] Code review confirms no other hardcoded secrets

## Work Log

### 2026-01-08 - Discovery
- Security sentinel agent identified hardcoded AES key
- Confirmed key is NIST AES-128 test vector (publicly known)
- Verified all devices use same key (source code audit)
- Assessed impact: Global password compromise risk
- Created remediation plan with phased approach

## Resources

- **NIST Test Vector**: https://csrc.nist.gov/projects/cryptographic-algorithm-validation-program/block-ciphers#AES
- **OWASP**: A02:2021 – Cryptographic Failures
- **CWE-321**: Use of Hard-coded Cryptographic Key
- **Best Practice**: NIST SP 800-132 (Password-Based Key Derivation)
- **Testing**: OpenSSL CLI for key derivation verification
- **Migration**: Script to re-encrypt passwords: `/root/migrate_passwords.sh`
