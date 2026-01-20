# 004: Global State Architecture Prevents Testing and Reusability

## Metadata
- **Status**: pending
- **Priority**: p1 (CRITICAL - Architecture)
- **Issue ID**: 004
- **Tags**: architecture, code-review, testability, global-state, refactoring
- **Dependencies**: None
- **Created**: 2026-01-08

## Problem Statement

The application uses **4 global variables** (`pipeline`, `loop`, `cmdArg`, `is_interrupted`) that are directly accessed by all modules. This violates dependency inversion principles and makes the code:
- **Untestable**: Cannot create isolated unit tests
- **Non-reusable**: Cannot instantiate multiple pipelines in one process
- **Thread-unsafe**: Multiple threads access globals without synchronization
- **Tightly coupled**: Every module depends on util.h global state

### Impact
- **Zero unit test coverage** (testing requires full hardware setup)
- Cannot run multiple instances (e.g., for multi-tenant scenarios)
- Race conditions on global state access
- Impossible to mock dependencies for testing
- Maintenance nightmare (hidden dependencies everywhere)

## Findings

### Location
`/home/jhw/ai/claude/projects/gstApp/util.h:185-189` (declarations)
`/home/jhw/ai/claude/projects/gstApp/util.cpp:17-21` (definitions)

### Global Variables
```cpp
// util.cpp:17-21
GstElement *pipeline = NULL;         // Global pipeline
GMainLoop *loop = NULL;              // Global event loop
volatile sig_atomic_t is_interrupted = 0;  // Global interrupt flag
gboolean is_live = FALSE;            // Global live flag (unused)
CmdArg cmdArg;                       // MASSIVE global config struct (183 lines!)
```

### Usage Across Codebase
**Every bin accesses global cmdArg**:
- `videoBin.cpp:90` - `cmdArg.levelMode`
- `videoBin.cpp:149` - `cmdArg.main_fps[videoData.csi]`
- `recordBin.cpp:476-477` - `cmdArg.cam[ch].bps[STREAM_REC]`
- `captureBin.cpp:94` - `cmdArg.cap.quality`
- `rtspServerBin.cpp:296` - `cmdArg.rtsp_port`
- **And 100+ more references across all files**

**Signal handlers access globals**:
- `util.cpp:34-47` - Accesses `pipeline`, `loop`, `is_interrupted`
- Thread-unsafe access without memory barriers

### Architecture Violations
- **Dependency Inversion Principle**: High-level bins depend on low-level global state
- **Single Responsibility**: Bins manage both GStreamer elements AND configuration
- **Open/Closed**: Cannot extend without modifying globals
- **Testability**: Cannot inject mock configuration or pipeline

## Proposed Solutions

### Solution 1: Encapsulate in PipelineContext Class (RECOMMENDED)
**Description**: Create context object to hold all global state, pass as parameter

**Implementation**:
```cpp
// New file: PipelineContext.h
class PipelineContext {
public:
    PipelineContext(const CmdArg& config);
    ~PipelineContext();

    GstElement* getPipeline() { return pipeline_; }
    GMainLoop* getMainLoop() { return loop_; }
    const CmdArg& getConfig() const { return config_; }

    void interrupt() { is_interrupted_ = true; }
    bool isInterrupted() const { return is_interrupted_; }

private:
    GstElement* pipeline_;
    GMainLoop* loop_;
    CmdArg config_;
    std::atomic<bool> is_interrupted_{false};
};

// Modify all Bin classes to accept context:
class VideoBin {
public:
    VideoBin(PipelineContext& ctx, int csi_num);
    bool init();

private:
    PipelineContext& ctx_;  // Reference to context
    int csi_num_;
};

// Usage in main:
PipelineContext ctx(cmdArg);
VideoBin videoBin[] = {
    VideoBin(ctx, 0),
    VideoBin(ctx, 1)
};
```

**Migration Plan**:
1. Create PipelineContext class
2. Update one Bin class to use context (VideoBin)
3. Test with modified Bin
4. Incrementally update remaining Bins
5. Remove global variables

**Pros**:
- Enables unit testing with mock context
- Supports multiple pipeline instances
- Clear ownership and lifecycle
- Modern C++ design

**Cons**:
- Large refactoring effort
- Touches every file
- Risk of breaking existing functionality

**Effort**: Large (1-2 weeks for full migration)
**Risk**: Medium (incremental approach reduces risk)

---

### Solution 2: Dependency Injection via Constructors
**Description**: Pass config and pipeline as constructor parameters to all bins

**Implementation**:
```cpp
class VideoBin {
public:
    VideoBin(const CameraConfig& config, GstElement* pipeline, int csi_num);

private:
    CameraConfig config_;  // Store only what we need
    GstElement* pipeline_;
    int csi_num_;
};

// Bins no longer access global cmdArg
int bps = config_.bps;  // Instead of: cmdArg.cam[ch].bps
```

**Pros**:
- Explicit dependencies (visible in signature)
- Enables testing with mock config
- Gradual migration possible

**Cons**:
- Still have global pipeline/loop
- Verbose constructor signatures
- Partial solution only

**Effort**: Medium (1 week)
**Risk**: Low (can do incrementally)

---

### Solution 3: Service Locator Pattern (NOT RECOMMENDED)
**Description**: Global registry that provides services on demand

```cpp
// Anti-pattern - DO NOT USE
class ServiceLocator {
    static PipelineContext* getContext() { return globalContext; }
};
```

**Pros**: Minimal code changes

**Cons**:
- Still global state (just hidden)
- Harder to test
- Violates explicit dependency principle

**Effort**: Small
**Risk**: High (creates more problems)

## Recommended Action

**Phase 1: Create Abstraction (1 week)**
1. Create PipelineContext class with getters for current globals
2. Instantiate as local variable in main() instead of globals
3. Pass reference to new code (parallel to old code)
4. Verify both paths work

**Phase 2: Migrate Bins (2 weeks)**
5. Update VideoBin to use PipelineContext
6. Update RecordBin, EncoderBin, etc.
7. Update MuxSinkBin, RtspServerBin
8. Update CaptureBin, AudioBin

**Phase 3: Remove Globals (1 week)**
9. Remove global declarations from util.h
10. Remove global definitions from util.cpp
11. Update signal handlers (can still use globals, POSIX requirement)
12. Full regression testing

**Total Effort**: 4 weeks
**Incremental**: Can deploy after each phase

## Technical Details

### Affected Files (ALL FILES)
**Headers**: util.h, all *Bin.h files
**Sources**: util.cpp, all *Bin.cpp, main.cpp, parser.cpp

### CmdArg Structure (183 lines!)
```cpp
typedef struct {
    // 183 lines of configuration fields
    const gchar* appname;
    gchar* mntDir;
    guint8 ch_enable;
    CamConfig cam[MAX_CHANNEL];
    guint16 fps[MAX_STREAM][MAX_CHANNEL];
    CaptureConfig cap;
    // ... 150+ more fields
} CmdArg;
```

### Signal Handler Special Case
Signal handlers MUST use global state (POSIX requirement):
```cpp
// These globals are OK (required by POSIX signal handling):
volatile sig_atomic_t is_interrupted = 0;

// Signal handler can only safely access sig_atomic_t
void handle_sigint(int sig) {
    is_interrupted = 1;  // Safe
    // Cannot call most functions here
}
```

## Acceptance Criteria

- [ ] PipelineContext class created with encapsulated state
- [ ] All Bin classes accept PipelineContext in constructor
- [ ] No direct global variable access except signal handlers
- [ ] Unit tests created for VideoBin using mock context
- [ ] Integration tests pass with new architecture
- [ ] Multiple pipeline instances can coexist (test with 2 instances)
- [ ] Thread safety verified (no data races on context access)
- [ ] Code review confirms no hidden global dependencies
- [ ] Documentation updated with new architecture diagram

## Work Log

### 2026-01-08 - Discovery
- Architecture strategist agent identified global state anti-pattern
- Pattern recognition specialist confirmed violation of SOLID principles
- Counted 100+ global variable accesses across codebase
- Assessed impact: Zero testability, tight coupling
- Designed phased migration approach to minimize risk

## Resources

- **Dependency Injection**: https://en.wikipedia.org/wiki/Dependency_injection
- **SOLID Principles**: https://en.wikipedia.org/wiki/SOLID
- **Testing Strategy**: Google Test framework for C++ unit tests
- **Refactoring**: "Working Effectively with Legacy Code" by Michael Feathers
- **Migration Guide**: Incremental refactoring to avoid big-bang rewrite
