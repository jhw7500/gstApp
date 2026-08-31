# Edge configuration stream arrays

The following optional keys under each `VHL_CAM.i2cN.chN` object use the same
exact two-element schema:

| Key | Shape | Order |
| --- | --- | --- |
| `bps` | two integers | `[record, rtsp]` |
| `gop` | two integers | `[record, rtsp]` |
| `profile` | two integers | `[record, rtsp]` |
| `quant` | two integers | `[record, rtsp]` |
| `qp_min` | two integers | `[record, rtsp]` |
| `qp_max` | two integers | `[record, rtsp]` |

Example:

```json
{
  "VHL_CAM": {
    "i2c2": {
      "ch0": {
        "bps": [4096, 1024],
        "gop": [15, 15],
        "profile": [9, 9],
        "quant": [-1, -1],
        "qp_min": [0, 0],
        "qp_max": [0, 0]
      }
    }
  }
}
```

Omitting one of these keys keeps its compiled-in defaults. An explicitly
present value is a startup-fatal configuration error when it is not an array,
does not contain exactly two elements, or contains a non-integer element.
`gstApp` reports the channel, key, expected length, and error kind for every
malformed array it finds, then exits before device access, pipeline
construction, or GStreamer state transitions. Array errors do not stop the
configuration traversal early; a separate fatal parser condition still can.

Value-range validation remains separate. Values that have the correct array
shape but fall outside an encoder's supported range continue to follow the
existing normalization and fallback policy in `ParserClass::check_arg()`.
