# CLove tests

End-to-end tests that exercise the FH scripting bindings through the real
engine (window, GL context, OpenAL all live), so they cover the layer where
bugs actually hide — the C ↔ script boundary.

## Running

Build CLove first (`./build_osx.sh` or `./build_linux.sh`), then:

```sh
./tests/run_tests.sh            # auto-detects build/clove
./tests/run_tests.sh path/to/clove
```

Exit code is 0 only if every test passes, so it drops straight into CI.

A short-lived window appears per test. To try a windowless run (only the
non-GL tests are guaranteed to work) set `CLOVE_HEADLESS=1` — it forces SDL's
dummy video driver.

## Layout

- `fh/test_*.fh` — positive tests. Each is a standalone CLove program whose
  `love_load()` runs assertions (failing via FH's `error()`), prints
  `CLOVE_TEST_OK`, then calls `love_event_quit()`. Pass = exit 0 **and** the
  marker was printed.
- `fh/xfail_*.fh` — negative tests, expected to exit non-zero. These lock in
  the argument-validation guards in the bindings (e.g. `setPixel` with too few
  arguments must error instead of reading past the argument array).

The runner copies each test to a scratch dir as `main.fh` (CLove loads
`main.fh` from the current directory) and runs the engine there.

## What's covered

| Test | Guards |
|------|--------|
| `test_quad` | `newQuad` normalisation + `setViewport`/`getViewport` round-trip (a binding read the wrong argument indices) |
| `test_imagedata` | `newImageData` + `setPixel`/`getPixel` round-trip and pixel independence |
| `test_math_noise` | simplex noise is deterministic, bounded to [-1,1], works in 1..4 D |
| `test_window` | window getters are self-consistent |
| `test_version` | `love_getVersion()` shape |
| `xfail_setpixel_argcount` | `setPixel` with missing channels must error |
| `xfail_getpixel_argcount` | `getPixel` without coords must error |
| `xfail_setviewport_argcount` | `setViewport` with a missing component must error |

## Adding a test

Copy an existing `fh/test_*.fh`, keep the `check()` helper and the
`love_load()` → `CLOVE_TEST_OK` → `love_event_quit()` shape, and assert
through the `love_*` API. Pure-logic and data bindings (math, image data,
quads, timers, window/version metadata) are the most reliable to assert on;
drawing output isn't checked.
