# Changelog

All notable changes to TinyUSB_Arduino are documented here.

This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Per-example config templates are now reachable.** Each example ships
  `examples/<Name>/tusb_config_arduinotinyusb.h`: a single-role config (device
  *or* host, only the classes that example uses). Previously these existed as
  `tusb_config.h` and were never read by any build — the README claimed the IDE
  picked them up automatically, which it does not, because the Arduino build
  puts the sketch folder on no include path for library sources. Copying the
  template over `src/tusb_config_arduinotinyusb.h` is what activates it, and
  that is now documented in the README with measured Flash/RAM figures per
  board.
- `src/config/tusb_config_union.h`: the full union as a named preset, so
  restoring the default after specializing is one `cp`. The active config
  `src/tusb_config_arduinotinyusb.h` is now a one-line include of it, which
  removes the duplicate copy of the union that restoring would otherwise need.

### Changed

- **Building the wrong example against a specialized config now fails at compile
  time**, on the missing `tud_*`/`tuh_*` call itself, instead of at link time or
  — worse — as a silently empty function body. `src/ArduinoTinyUSB.h` guards the
  role-specific entry points on `CFG_TUD_ENABLED` / `CFG_TUH_ENABLED`.
- The per-example configs no longer carry an ESP32 `#include_next` branch whose
  stated premise (that the sketch directory precedes the vendored config on the
  include path) does not hold.

- **Library-wide `CFG_TUSB_DEBUG` default lowered from `2` to `0`** (the
  upstream TinyUSB default), in both `src/tusb_config_arduinotinyusb.h` and
  `src/config/tusb_config_common.h`. Internal `TU_LOG` output is now off unless
  asked for; this removes ISR-context blocking that could starve a 1 ms
  isochronous audio frame. Both headers remain `#ifndef`-guarded, so
  `-DCFG_TUSB_DEBUG=2` still re-enables logging for bring-up without editing
  files. `TU_ASSERT` still fires on failure (and still traps under a
  debugger) — only its log line is gone. `CFG_TUD_TASK_QUEUE_SZ` and
  `CFG_TUH_TASK_QUEUE_SZ` stay at 64; they are no longer justified by
  logging latency but must not be trimmed.

## [1.0.0] — first public release

### Supported

**Arduino GIGA R1 WiFi.** Device (USB Type C) and host (USB Type A) run
simultaneously on the board's two independent USB controllers.

### Compile-only, unsupported

Pico, Pico W, Pico 2, Pico 2W, ESP32-S2, ESP32-S3, Arduino Due, Arduino Zero,
Nano 33 IoT, M0 Pro and Adafruit Grand Central M4 build and are compile-checked,
but they are **not tested** and carry **no support commitment**. "Compile-only"
means: it compiles, and that is all that is promised.

### Not supported

Uno R4 Minima and Nano R4 (`renesas_uno`) are not supported. They would require
patching the installed Renesas core in place, which this library does not do. An
R4 core fails loudly at compile time with an "Unsupported board" error rather
than misbehaving silently.

### Notes

- Vendored TinyUSB 0.21.0, commit `b80f1c107d0a33eb3be055f95fe0b3b9d6c0be48`,
  MIT licensed. Local deviations are listed in `src/VERSION`.
- This is a USB layer only. It has no dependency on any sound or synthesis
  library; audio here is a USB *class*, not a sound source.

[1.0.0]: https://github.com/controllercustom/TinyUSB_Arduino/releases/tag/v1.0.0
