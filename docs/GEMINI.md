# GEMINI Project Documentation

## Project: gstApp
**Path**: `/home/jhw/ai/claude/projects/gstApp`

## Overview
`gstApp` is a high-performance camera application for the i.MX8MP platform, utilizing GStreamer for recording, RTSP streaming, and still image capture. It ensures data integrity through a robust **"Write-to-Temp -> Verify -> Safe Move"** pipeline.

## Recent Updates (2026-01-27)
We have addressed critical issues regarding file splitting synchronization and GStreamer compatibility.

### Key Improvements
1.  **Unified Session Management**: 
    - Changed timestamp extraction logic to group all channels (video & subtitle) by `YYYYMMDD_HHMM` (minute precision).
    - `gstApp` creates `.mp4.part` and `vcm` creates `.srt.part` files.
    - A single `/tmp/session_<timestamp>.all_done` signal triggers the safe move for all files in that minute.

2.  **GStreamer 1.18+ Compatibility**:
    - Fixed the issue where `splitmuxsink` signals (`fragment-closed`) were not firing.
    - Switched to monitoring `splitmuxsink-fragment-closed` bus messages in `main.cpp` to reliably detect file completion.

3.  **Robust File Operations**:
    - `chk_cam_operate.sh` now automatically fixes relative paths to absolute paths.
    - Implemented a generalized `ProcessFile` function that handles both video and subtitle files uniformly.
    - Enforced a 3-step sync (File -> Dir -> Target) to prevent data corruption during power loss.

## Remaining Tasks (Technical Debt)
Although the core functionality is stable, the following improvements are recommended for long-term reliability:

1.  **GStreamer Handler Optimization**: Move file I/O operations from the main bus watch loop to a dedicated work queue to prevent pipeline blocking during high I/O latency.
2.  **Active Disk Management**: Implement proactive old file deletion (recycling) when disk usage exceeds 95%, rather than just logging errors.
3.  **Time Jump Handling**: Refactor `CleanupStaleFiles` to use monotonic clock or handle system time changes (NTP sync) to prevent accidental deletion of valid recording files.

## Documentation
*   **[System Architecture](docs/ARCHITECTURE.md)**: Overview of `CaptureBin`, `MuxSinkBin`, and the signaling mechanism.
*   **[Work Note](docs/WORK-NOTE.md)**: Detailed log of the refactoring process and validation results.
