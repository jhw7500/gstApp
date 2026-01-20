# AGENTS.md — gstApp

This file is guidance for agentic coding in `gstApp/`.

## Scope
- Repository: `/home/jhw/ai/claude/projects/gstApp`
- Language: C++ with GLib/GStreamer APIs
- Build system: Makefile only
- Cursor rules: **none found** (`.cursor/rules/` or `.cursorrules` not present)
- Copilot rules: **none found** (`.github/copilot-instructions.md` not present)

## Quick Commands (from `Makefile`)
- Build (default target): `make`
- Clean: `make clean`

### Build Artifacts
- Output binary: `bin/gstApp`
- Object files: `obj/*.o` (moved by the `gstApp` target)

### Dependencies (from `Makefile`)
- `pkg-config` is used to resolve libs and flags.
- Required libs include:
  - `gstreamer-1.0`
  - `gstreamer-rtsp-server-1.0`
  - `gstreamer-plugins-base-1.0`
  - `gstreamer-app-1.0`
  - `gstreamer-video-1.0`
  - `glib-2.0`
  - `json-c`
  - `openssl`
  - `check`
- Additional link flags include `-lturbojpeg` and a hardcoded rnnoise path:
  - `-L/opt/desktop/gitlab/gst-jhw/gstapp/gstapp/app/rnnoise/lib -lrnnoise`
  - `-I/opt/desktop/gitlab/gst-jhw/gstapp/gstapp/app/rnnoise/include`

### Lint / Format
- No lint target in `Makefile`.
- No formatter config (`.clang-format`, `.editorconfig`, etc.) in repo.
- Formatting is manual—match existing style in touched files.

### Tests
- No test target or runner in `Makefile`.
- `testBin.cpp` exists, but no test harness detected.
- Single-test invocation: **not available** (no test framework configured).

## Code Style Guidelines (observed patterns)

### Formatting
- Indentation: 4 spaces (spaces, not tabs).
- Brace style: Allman (opening brace on next line).
- Blank lines separate logical blocks in functions.
- `#if 0` is used for large commented-out blocks; avoid adding new ones.

### Includes
- Local headers first, then system headers:
  - Example order: project headers (`"util.h"`), then `<fcntl.h>`, `<unistd.h>`.
- Prefer minimal includes needed for the file.

### Types
- Prefer GLib types: `gint`, `guint8`, `gboolean`, `gchar*`, `gsize`, `gdouble`.
- Use `const gchar *` for constant strings.
- Enums are `typedef enum { ... } Name;` with uppercase enumerators.

### Naming
- Classes: `PascalCase` (e.g., `ParserClass`).
- Methods / functions: `camelCase` (e.g., `addSignalHandler`, `json_object_get_value`).
- Macros / constants: `UPPER_SNAKE_CASE` (e.g., `DEFAULT_RTSP_PORT`).
- Structs: `typedef struct { ... } Name;` (capitalized type name).

### Error Handling
- Log errors with `__LOG(...)` using `_FILE_` and `__LINE__`.
- Return `-1` or `FALSE` on failure; check for NULLs defensively.
- Do not ignore return values for GLib/GStreamer APIs.
- Prefer safe wrappers (`safe_write_file`, `safe_mkdir_p`, `safe_exec_i2c`) instead of `system()`.

### Memory Management
- GLib allocations must be freed with `g_free` (e.g., `g_strdup_printf`).
- `GError*` must be freed with `g_error_free`.
- `GDateTime*` must be unref’d with `g_date_time_unref`.
- GStreamer objects should be unref’d using `gst_object_unref` where required.

### Logging
- Use existing logging macros and categories (`__LOG`, `LOG_*`).
- Keep log messages consistent with the prefix format: `[TAG][file:line] ...`.

### Concurrency & Signals
- Signal handlers log via `__LOG` and signal the GStreamer pipeline.
- Follow existing patterns for signal setup/teardown in `util.cpp`.

### Configuration & Constants
- Defaults are centralized in headers (`parser.h`, `util.h`), not duplicated.
- Avoid hardcoding values in `.cpp` files if a constant already exists.

### C/C++ Usage
- Code is C++ but uses C-style APIs heavily (GLib/GStreamer).
- Avoid introducing STL containers unless there is a clear pattern in the file.
- Prefer `g_*` utilities (e.g., `g_strdup_printf`, `g_getenv`).

## File Structure
- Most components are split into `.h`/`.cpp` pairs:
  - `videoBin.*`, `recordBin.*`, `muxSinkBin.*`, `rtspServerBin.*`, etc.
- Keep changes localized to the component’s pair when possible.

## JSON & Parser Patterns
- JSON access uses `json_object_object_get` and `json_object_get_*` with type checks.
- Log missing keys as errors and return `-1` (see `ParserClass::json_object_get_value`).
- Use `compareBuf()` for fixed-length token comparison where already used.

## String Handling
- `g_strdup_printf` is common for formatted strings; free with `g_free`.
- Use `strdup` only when ownership requires `free()`/`g_free()` symmetry.
- Keep string literals in headers as `DEFAULT_*` macros.

## Practical Guidance for Agents
- Match the style of the file you are editing (indentation and brace placement).
- Reuse `__LOG` for diagnostics; do not add new logging frameworks.
- Do not add new build systems or dependencies without explicit request.
- Avoid large refactors when fixing a bug; prefer minimal changes.

## Runtime & Debugging Notes
- `GST_DEBUG_DUMP_DOT_DIR` enables dot graph dumps (see signal handling in `util.cpp`).
- SIGINT/SIGHUP handling triggers EOS and optional dot snapshot dumps.
- Fault handler prints a gdb hint for `gstApp` on crashes.

## Security/Robustness Expectations
- Do not introduce `system()`/`popen()` calls; use existing safe helpers.
- Validate external inputs before using them in file paths or device commands.
- Prefer explicit error propagation over silent failure.

## Globals & Shared State
- Global state exists in `util.h` (`pipeline`, `loop`, `cmdArg`, `is_interrupted`).
- Avoid adding new global state; thread safety is fragile.

## References
- Build configuration: `Makefile`
- Core headers: `util.h`, `parser.h`
- Main entry point: `main.cpp`
