# GEMINI Project Documentation

## Project: gstApp
**Path**: `/home/jhw/ai/claude/projects/gstApp`

## Overview
`gstApp` is a high-performance camera application for the i.MX8MP platform, utilizing GStreamer for recording, RTSP streaming, and still image capture.

**(Korean documentation is available: [한글 문서 보기](docs/ko/GEMINI_KO.md))**

## Recent Updates (2026-01-09)
We have addressed critical performance and stability issues related to the capture subsystem.

### Key Improvements
1.  **Zero-Latency Command Processing**: Switched from blocking wait to an asynchronous request queue system.
2.  **CPU Optimization**: implemented intelligent valve control to stop encoding pipeline when idle.
3.  **Robustness**: Added timeout protection and thread-safe memory management.

## Documentation
Detailed documentation has been organized into the `docs/` directory:

*   **[System Architecture](docs/ARCHITECTURE.md)**: Overview of the application structure, key classes (`CaptureBin`, `ParserClass`), and data flow.
*   **[Optimization Log](docs/OPTIMIZATION.md)**: Detailed report of the bugs fixed, performance bottlenecks resolved, and the specific implementation strategies used.

## Directory Structure (Conceptual)
```
/home/jhw/ai/claude/
├── GEMINI.md (This file)
└── projects/
    └── gstApp/
        ├── main.cpp
        ├── captureBin.cpp
        ├── captureBin.h
        ├── parser.cpp
        └── docs/
            ├── ARCHITECTURE.md
            ├── OPTIMIZATION.md
            └── ko/ (Korean Translations)
```
*Note: Due to environment restrictions, this `GEMINI.md` is currently located in `projects/gstApp/GEMINI.md`.*