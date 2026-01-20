# 005: 1500-Line Command Parser Function (God Function Anti-Pattern)

## Metadata
- **Status**: pending
- **Priority**: p2 (HIGH - Code Quality)
- **Issue ID**: 005
- **Tags**: code-quality, code-review, refactoring, maintainability, complexity
- **Dependencies**: 004 (benefits from context refactoring)
- **Created**: 2026-01-08

## Problem Statement

The `cmd_parser()` function in parser.cpp is **1516 lines long** with 80+ nested if-else comparisons, making it:
- Impossible to unit test individual commands
- Difficult to add new commands without breaking existing ones
- High cyclomatic complexity (unmaintainable)
- Massive code duplication (same patterns repeated 40+ times)

### Impact
- **80% of parser.cpp** is this single function
- Cannot add commands without touching 1500-line function
- Bug fixes risky (hard to reason about control flow)
- Code review nearly impossible (too long to review)

## Findings

### Location
`/home/jhw/ai/claude/projects/gstApp/parser.cpp:739-2255` (1516 lines!)

### Code Structure
```
Simplification opportunity found across all the files.
```

This is the second-longest function in our C++ codebase.

### Complexity Metrics
- **Lines**: 1516
- **Cyclomatic Complexity**: ~80 (should be <10)
- **Nesting Depth**: 5-6 levels deep
- **Commands Handled**: ~40 different commands
- **Code Duplication**: 60-70% (same pattern repeated)

### Example of Repeated Pattern
```cpp
// This exact pattern appears 40+ times with minor variations:
if (compareBuf(token, "get", 3)) {
    token = strtok(NULL, SPLIT_CHAR);
    if (compareBuf(token, "bps", 3)) {
        token = strtok(NULL, SPLIT_CHAR);
        if (compareBuf(token, "rec", 3)) {
            for (i = 0; i < MAX_CHANNEL; i++)
                if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_REC])
                    recordBin[i].getBitrate();
        }
        else if (compareBuf(token, "rtsp", 4)) {
            for (i = 0; i < MAX_CHANNEL; i++)
                if (cmdArg.cam[i].enable && cmdArg.stream_en[STREAM_RTSP])
                    rtspServerBin[i].getBitrate();
        }
    }
    else if (compareBuf(token, "fps", 3)) {
        // ... exact same structure for fps
    }
    else if (compareBuf(token, "gop", 3)) {
        // ... exact same structure for gop
    }
    // ... repeated 35+ more times
}
```

## Proposed Solutions

### Solution 1: Table-Driven Command Dispatcher (RECOMMENDED)
**Description**: Replace 1500 lines of if-else with command table and handlers

**Implementation**:
```cpp
// Command handler function type
typedef int (*CommandHandler)(const char* args, void* context);

// Command table entry
struct Command {
    const char* name;
    CommandHandler handler;
    const char* help;
};

// Example handlers (much shorter and testable)
int handleGetBps(const char* stream, void* ctx) {
    bool is_rec = strcmp(stream, "rec") == 0;
    bool is_rtsp = strcmp(stream, "rtsp") == 0;

    for (int i = 0; i < MAX_CHANNEL; i++) {
        if (!cmdArg.cam[i].enable) continue;
        if (is_rec && cmdArg.stream_en[STREAM_REC])
            recordBin[i].getBitrate();
        if (is_rtsp && cmdArg.stream_en[STREAM_RTSP])
            rtspServerBin[i].getBitrate();
    }
    return 0;
}

int handleSetBps(const char* args, void* ctx) {
    // Parse args: "rec 0 5000" -> stream, channel, value
    char stream[16];
    int channel, value;
    if (sscanf(args, "%15s %d %d", stream, &channel, &value) != 3)
        return -1;

    if (strcmp(stream, "rec") == 0)
        recordBin[channel].setBitrate(value);
    else if (strcmp(stream, "rtsp") == 0)
        rtspServerBin[channel].setBitrate(value);

    return 0;
}

// Command table (replaces 1500 lines!)
static Command commands[] = {
    {"get bps", handleGetBps, "Get bitrate for stream"},
    {"set bps", handleSetBps, "Set bitrate for stream"},
    {"get fps", handleGetFps, "Get framerate"},
    {"set fps", handleSetFps, "Set framerate"},
    // ... ~20 commands total
    {NULL, NULL, NULL}
};

// New cmd_parser (50 lines instead of 1500)
int ParserClass::cmd_parser(const gchar* buffer, int len, void* arg) {
    char cmd_buf[256];
    strncpy(cmd_buf, buffer, sizeof(cmd_buf)-1);

    // Tokenize command
    char* cmd = strtok(cmd_buf, " ");
    char* subcmd = strtok(NULL, " ");
    char* args = strtok(NULL, "\0");  // Rest of line

    // Build command string
    char full_cmd[64];
    snprintf(full_cmd, sizeof(full_cmd), "%s %s", cmd, subcmd);

    // Look up in table
    for (Command* c = commands; c->name != NULL; c++) {
        if (strcmp(full_cmd, c->name) == 0) {
            return c->handler(args, arg);
        }
    }

    __LOG(LOG_ERR, "Unknown command: %s", full_cmd);
    return -1;
}
```

**Pros**:
- Reduces 1516 lines to ~200-300 lines total
- Each handler is testable independently
- Easy to add new commands (just add table entry)
- Clear separation of concerns
- Command help text built-in

**Cons**:
- Requires refactoring existing code
- Need to design handler argument passing

**Effort**: Large (1 week)
**Risk**: Medium (but can test handlers individually)
**LOC Reduction**: ~1200 lines (80%)

---

### Solution 2: Command Pattern (OOP Approach)
**Description**: Use object-oriented command pattern

**Implementation**:
```cpp
class ICommand {
public:
    virtual ~ICommand() {}
    virtual int execute(const char* args) = 0;
    virtual const char* getName() const = 0;
};

class GetBpsCommand : public ICommand {
public:
    int execute(const char* args) override {
        // Implementation here
    }
    const char* getName() const override { return "get bps"; }
};

class SetBpsCommand : public ICommand {
    // Similar structure
};

// Registry
class CommandRegistry {
    std::map<std::string, ICommand*> commands_;
public:
    void registerCommand(ICommand* cmd) {
        commands_[cmd->getName()] = cmd;
    }
    int execute(const std::string& name, const char* args) {
        auto it = commands_.find(name);
        if (it != commands_.end())
            return it->second->execute(args);
        return -1;
    }
};
```

**Pros**:
- Very OO, extensible
- Each command is a separate class (easy to test)
- Can add decorators, chains, etc.

**Cons**:
- More boilerplate than table approach
- Requires C++ (current code is C-style)
- More files to manage

**Effort**: Large (2 weeks)
**Risk**: Medium

---

### Solution 3: State Machine Parser
**Description**: Formal state machine for command parsing

**Pros**: Very structured
**Cons**: Overkill for this use case
**Not Recommended**: Table-driven approach is simpler

## Recommended Action

**Phase 1: Preparation (3 days)**
1. Extract 5 most common command patterns
2. Create handler functions for these 5
3. Test handlers independently
4. Create command table infrastructure

**Phase 2: Migration (1 week)**
5. Convert 10 commands at a time to table-driven
6. Test after each batch
7. Remove old if-else code as commands are migrated
8. Keep both systems parallel during migration

**Phase 3: Cleanup (2 days)**
9. Remove all old if-else code
10. Add comprehensive command tests
11. Document command addition process

## Technical Details

### Affected Files
- `/home/jhw/ai/claude/projects/gstApp/parser.cpp` (primary)
- `/home/jhw/ai/claude/projects/gstApp/parser.h` (add command table)
- New file: `commands.{h,cpp}` (optional, for handler functions)

### Current Commands (partial list)
```
get/set: bps, fps, gop, rotate, quality, crop, overlay, ae
start/stop: rec, rtsp, cap
config: resolution, bitrate, format
debug: state, version, log
Special: cmd (SECURITY RISK - see issue #001)
```

### Dependencies
Benefits from Issue #004 (global state refactoring) - handlers can use context instead of globals

## Acceptance Criteria

- [ ] Command table structure defined
- [ ] At least 20 handler functions created
- [ ] cmd_parser() reduced to <100 lines (dispatcher only)
- [ ] Each handler is independently testable
- [ ] Unit tests for 10+ handlers
- [ ] All existing commands still functional
- [ ] Command help text available (cmd table provides this)
- [ ] Documentation for adding new commands
- [ ] Code review confirms <50 lines per handler function
- [ ] Cyclomatic complexity <10 for dispatcher

## Work Log

### 2026-01-08 - Discovery
- Code simplicity reviewer identified 1516-line function
- Pattern recognition specialist found 60-70% code duplication
- Calculated cyclomatic complexity: ~80 (unmaintainable)
- Counted ~40 commands with repeated patterns
- Designed table-driven refactoring approach

## Resources

- **Design Pattern**: Command Pattern
- **Refactoring**: "Replace Conditional with Polymorphism" (Fowler)
- **Testing**: Table-driven approach enables unit testing
- **Cyclomatic Complexity**: Should be <10 for maintainability
- **Similar Work**: Linux kernel uses table-driven command dispatch
