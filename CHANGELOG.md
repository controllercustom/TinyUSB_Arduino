# Changelog

All notable changes to TinyUSB_Arduino are documented here.

This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

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
