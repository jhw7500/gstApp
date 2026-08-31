# Edgeconf Array Startup-Fatal Design

## Goal

An explicitly present but malformed per-stream integer array in `edgeconf_*.json`
must stop `gstApp` before device access, pipeline construction, or GStreamer
state transitions. Missing optional arrays must continue to use their
initialized defaults.

## Scope

The affected keys are `bps`, `gop`, `profile`, `quant`, `qp_min`, and `qp_max`
under each `VHL_CAM.i2cN.chN` object. Every key is an exact two-element array in
`[record, rtsp]` order. `CFG_ARR_NOT_ARRAY`, `CFG_ARR_BAD_LEN`, and
`CFG_ARR_BAD_ELEM` are fatal structural errors. `CFG_ARR_MISSING` is not fatal.

Existing fallback policies for optional booleans, crop settings, encoder names,
and numeric ranges remain recoverable. This change must not turn the existing
general `g_cfg_errors` counter into a startup-fatal gate.

## Behavior

`json_parser()` uses a parse-local array-error counter and does not return early
because of an array error. It therefore records malformed arrays across the
remaining fields and channels unless a separate pre-existing fatal parser
condition interrupts traversal. Each diagnostic identifies the channel, key,
expected length, and error kind. After traversal, one or more structural array
errors produce a critical summary and a negative return value.

`main.cpp` already checks a negative `json_parser()` result before argument
normalization, owner-lock acquisition, sysfs writes, video-bin construction,
MAX9296 preparation, and GStreamer state transitions. That existing gate is the
startup stop point.

## Verification

- A real `ParserClass::json_parser()` integration test uses temporary
  `edgeconf_*.json` fixtures and the production parser/cfgjson implementation.
- Malformed arrays return negative while preserving initialized defaults.
- Parsing continues after the first malformed array so later channel values and
  diagnostics are still produced.
- Missing arrays and pre-existing recoverable boolean errors return success.
- The startup source contract verifies the negative parser result exits before
  hardware initialization.
- The complete application and focused test binaries cross-compile for i.MX8.
