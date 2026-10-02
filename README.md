# TinyUSB_Arduino

A TinyUSB device and host stack for Arduino boards, wrapping upstream TinyUSB
behind a thin Arduino Library API. Unlike other TinyUSB Arduino libraries
this is not a subset of USB classes. All examples are available converted to
sketches and using this library's API.

OpenCode coding agent and a variety of coding LLMs used to develop this library.

This project is not part of the TinyUSB project so **DO NOT** report problems with
this project to the TinyUSB project. 

This is an experimental/proof of concept project made public. There is no
commitment to keep this up to date, sync with upstream, regression test, accept
PRs, etc. For amusement purposes only.

Here is an example of what can be done using this library on Giga.

The Mozzi synth project has been ported to Giga so start with this. Add USB
device audio output on the Giga's Type C USB device port. The Giga appears to
be a USB microphone to the PC. Audacity can record Mozzi output. Digital all
the way through.

Add USB host MIDI on the Giga's Type A USB host port. Connect a nanoKONTROL2 or
any USB MIDI control surface. Map knobs, faders, and buttons to Mozzi inputs.

SAM3X/Due is not supported by TinyUSB but this project includes a port
developed by AI coding. Due is not recommended because arduino-sam depends on a
old version of gcc. But if you have a Due board gathering dust, this project
might make it useful. **DO NOT** report SAM3X/Due problems to the TinyUSB
project because this feature is exclusive to this project.

## Supported: Arduino GIGA R1 WiFi

The GIGA R1 has **two independent USB controllers**:

| Port | Controller | Role |
|---|---|---|
| USB Type C | OTG_FS, rhport 0 | device |
| USB Type A | OTG_HS, rhport 1 | host |

So a single sketch can run **device and host at the same time**, on separate
controllers and separate interrupts. That is the property this library is built
around, and the reason the GIGA is the supported board.

```cpp
#include <ArduinoTinyUSB.h>

void setup() {
  Serial1.begin(115200);
  tud_arduino_init();   // device, Type C
  tuh_arduino_init();   // host,   Type A  (also enables VBUS)
}

void loop() {
  tud_arduino_task();
  tuh_arduino_poll();
}
```

## Other boards

The boards below **build and are compile-checked**, but they are **not tested**
and carry **no support commitment**. "Compile-only" means exactly that: it
compiles, and nothing more is promised.

| Board | FQBN | Console |
|---|---|---|
| Arduino Due | `arduino:sam:arduino_due_x` | `Serial` |
| Arduino Zero | `arduino:samd:arduino_zero_edbg` | `Serial` |
| Nano 33 IoT | `arduino:samd:nano_33_iot` | `Serial1` |
| M0 Pro | `arduino:samd:mzero_pro_bl_dbg` | `Serial` |
| Grand Central M4 | `adafruit:samd:adafruit_grandcentral_m4:usbstack=tinyusb` | `Serial1` |
| Pico / Pico W | `rp2040:rp2040:rpipico:usbstack=nousb,dbgport=Serial1` | `Serial1` |
| Pico 2 / Pico 2W | `rp2040:rp2040:rpipico2:usbstack=nousb,dbgport=Serial1` | `Serial1` |
| ESP32-S2 / ESP32-S3 | `esp32:esp32:esp32s2` / `esp32s3` | `Serial` (UART0) |

On every board except the GIGA R1, TinyUSB owns the native USB port, so it is
**either** a device **or** a host, never both at once.

**Uno R4 / Nano R4 are not supported.** They would need the installed Renesas
core patched in place, which this library does not do. An R4 core fails loudly
at compile time with an "Unsupported board" error.

## Installation

To install the library from GitHub in Arduino IDE 2.x, first download the library
as a ZIP file from the GitHub page. Then, open the Arduino IDE, go to Sketch >
Include Library > Add .ZIP Library, and select the downloaded ZIP file to
complete the installation.

No compiler flags are needed. Board selection is automatic, from the core's own
macros, so the IDE and `arduino-cli` both work with no project settings.

## Quick start: device

```cpp
#include <ArduinoTinyUSB.h>

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);
  tud_arduino_init();
}

void loop() {
  tud_arduino_task();
}
```

USB descriptors with `extern "C"` callbacks go in a **separate `.cpp` tab**. The
Arduino `.ino` prototype generator mangles `extern "C"` signatures, so putting
them in the `.ino` breaks the build. Every `examples/Device_*` sketch shows the
two-tab pattern.

## Quick start: host

```cpp
#include <ArduinoTinyUSB.h>

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);
  tuh_arduino_init();
}

void loop() {
  tuh_arduino_poll();
}
```

## API

| Function | Purpose |
|---|---|
| `tud_arduino_init()` | start the device stack |
| `tuh_arduino_init()` | start the host stack (GIGA also enables VBUS) |
| `tud_arduino_task()` | pump the device stack from `loop()` |
| `tuh_arduino_poll()` | pump the host stack from `loop()` |
| `arduino_tinyusb_get_mode()` | current mode (device / host / uninitialised) |
| `arduino_tinyusb_probe()` | print MCU register state, for debugging |

Macros: `ARDUINO_TINYUSB_CONSOLE` (default `Serial` or `Serial1` per board, see
the table above), `ARDUINO_TINYUSB_BAUD` (default `115200`).

Override the console if the default is wrong for your wiring:

```cpp
#define ARDUINO_TINYUSB_CONSOLE Serial2
#define ARDUINO_TINYUSB_BAUD    921600
#include <ArduinoTinyUSB.h>
```

### Why the console is not always `Serial`

On several boards `Serial` **is** the native USB port, which TinyUSB then owns.
Printing there would collide with the USB stack. Those boards default to
`Serial1` (the UART pins) instead. This surprises people, so the table above
lists the default per board.

## Examples

34 sketches: 22 device and 12 host.

**Device** — `CDC`, `CDC_Dual`, `CDC_MSC`, `CDC_MSC_Bench`, `CDC_UAC2`, `Audio`,
`DFU_RT`, `Dynamic_Config`, `HID`, `HID_Boot`, `HID_Composite`, `HID_Generic`,
`HID_Multi`, `Keyboard`, `MIDI`, `MIDI_Seq`, `MSC`, `MSC_Dual`, `NET`, `USBTMC`,
`Vendor`, `WebUSB`

**Host** — `Audio`, `BareAPI`, `CDC`, `DeviceInfo`, `HID`, `HID_Boot`,
`HID_Controller`, `MIDI`, `MIDI_Rx`, `MSC`, `MSC_FatFS`, `Printer`

Start with `examples/Device_CDC` or `examples/Host_CDC`; both are minimal and
exercise the paths most people need.

Two host examples are board-limited:

- `Host_MSC_FatFS` is **Due and Zero only**. It bundles its own copy of ChaN's
  FatFs, and the Mbed core (GIGA) already defines the same `disk_*` symbols, so
  the link fails with duplicate symbols there.
- `Host_Printer` and `Host_Audio` need the printer and audio host drivers
  respectively; both are present in this tree, but they are the least exercised
  paths.

## Configuration

Each example carries its own `tusb_config.h`, so a sketch's settings live with
the sketch. You do not need to touch `compiler.extra_flags` — the IDE picks the
example's config up automatically.

The library-wide defaults live in `src/tusb_config_arduinotinyusb.h`. Common
knobs:

| Macro | Notes |
|---|---|
| `CFG_TUSB_DEBUG` | 0 = off, 1 = errors, 2 = verbose. **Use 0 for isochronous audio**: logging runs in the USB ISR, and a 40-character line at 115200 baud blocks for ~3.5 ms — longer than a 1 ms audio frame. |
| `CFG_TUD_CDC` | must be `2`. A unified config serves examples with two CDC interfaces, and `cdcd_open()` asserts on the second. |
| `CFG_TUD_CDC_RX_BUFSIZE` / `_TX_BUFSIZE` | per-instance CDC buffers |

## Per-board build notes

- **Grand Central M4** — `usbstack=tinyusb` is **mandatory** in the FQBN. The
  board defines `USBCON` at board level, so the core claims native USB before
  `setup()` runs.
- **GIGA R1** — the Type A host port sources its own VBUS. Other host ports may
  not, so a self-powered device (or a powered hub) is safest.
- **ESP32** — examples print to UART0, not native USB, because TinyUSB owns
  native USB. Release GPIO19/20 before TinyUSB takes the port.
- **Due** — shared endpoint numbers cannot flip direction at runtime: the SAM3X
  `DEVEPTCFG.EPDIR` bit is write-once in silicon. The Due examples therefore
  use distinct physical endpoints per direction. This is visible in the
  descriptor files as `#if defined(ARDUINO_TINYUSB_BOARD_DUE)` blocks.

## Licence

MIT — see [LICENSE](LICENSE). Bundles TinyUSB (MIT, Ha Thach) and FatFs
(BSD-style, ChaN, used by the `Host_MSC_FatFS` example).

Vendored TinyUSB version and the local modifications to it are recorded in
[`src/VERSION`](src/VERSION).

## Issues

<https://github.com/controllercustom/TinyUSB_Arduino/issues>

