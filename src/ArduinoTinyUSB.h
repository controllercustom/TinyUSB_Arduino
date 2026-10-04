/*
 * ArduinoTinyUSB.h — Unified public API header for Arduino-TinyUSB library.
 *
 * Board selection: automatic via ArduinoTinyUSB/board_auto.h (core macros),
 * or define exactly ONE of these before including this header,
 * or via compiler flags:
 *   -DARDUINO_TINYUSB_BOARD_DUE       (Tier B, compile-only)
 *   -DARDUINO_TINYUSB_BOARD_ZERO      (Tier B, compile-only)
 *   -DARDUINO_TINYUSB_BOARD_GCM4      (Tier B, compile-only)
 *   -DARDUINO_TINYUSB_BOARD_GIGA
 *   -DARDUINO_TINYUSB_BOARD_PICO
 *   -DARDUINO_TINYUSB_BOARD_ESP32
 */

#pragma once

// Auto-detect first: explicit -D / #define wins, otherwise core macros decide.
#include "ArduinoTinyUSB/board_auto.h"

#include <Arduino.h>

// Conflict workaround: SAMD core USBCore.h defines these.
// Must stay AFTER Arduino.h (which pulls USBCore.h in on SAMD, defining them
// as macros) and BEFORE the tusb.h include below, where msc.h needs the real
// enums.
#undef MSC_SUBCLASS_SCSI
#undef MSC_PROTOCOL_BULK_ONLY

// TinyUSB core header — always the vendored copy in src/.
// Included HERE, before the API declarations further down, because those are
// guarded on CFG_TUD_ENABLED / CFG_TUH_ENABLED and tusb.h is what resolves
// the active config (src/tusb_config_arduinotinyusb.h) that defines them.
#ifdef __cplusplus
extern "C" {
#endif
  #include "tusb.h"
#ifdef __cplusplus
}
#endif

// Console Defaults
// Due: Serial = UART programming port (ATmega16U2 bridge), safe to use.
// Zero/M0 Pro: Serial = EDBG SERCOM5 UART (SERIAL_PORT_MONITOR), independent
//   of the native USB peripheral. Verified on hardware: host mode on native
//   USB plus a Serial.print heartbeat on the programming port run
//   concurrently. bsp_zero.cpp's strong USB_Handler only dispatches USB_IRQn
//   (dcd/hcd_int_handler) and never touches SERCOM5_IRQn, so Serial stays
//   alive in TinyUSB host AND device mode — no FTDI/UART bridge needed.
// Nano 33 IoT (same BOARD_ZERO key): Serial == SerialUSB (native, see
//   variants/nano_33_iot/variant.h), owned by TinyUSB — uses Serial1.
// GIGA: Serial1 = UART pins (unified default; keeps device-mode safe when
//   TinyUSB owns OTG_FS Type C).
// GCM4: Serial = native USB CDC (owned by TinyUSB in device mode) and there
//   is no USB-serial chip on the board — console is Serial1 (SERCOM0 on
//   D0 RX / D1 TX) via an external UART bridge (CP210x). NOTE: this chain
//   is duplicated in ArduinoTinyUSB/bsp_common.h; keep the two in sync.
//   Getting it wrong here is silent: the sketch's `Serial.begin()` opens the
//   native-USB CDC, whose 64-byte TX buffer silently swallows the
//   CFG_TUSB_DEBUG banner (~600 chars) and then deadlocks setup() in
//   Serial_::write — no USB, no console, and the stack pointer ends up
//   inside Print::write.
// Pico (usbstack=nousb): Serial (SerialUSB) doesn't exist; Serial1 (UART0 on
//   GP0/GP1) is auto-started by the core (DEBUG_RP2040_PORT) — use Serial1.
// ESP32 (USBMode OTG): native USB owned by TinyUSB — use Serial (UART0,
//   devkit USB-UART bridge) for the console.
#if !defined(ARDUINO_TINYUSB_CONSOLE)
  #if defined(ARDUINO_TINYUSB_BOARD_DUE)
    #define ARDUINO_TINYUSB_CONSOLE Serial
  #elif defined(ARDUINO_TINYUSB_BOARD_ZERO)
    #if defined(ARDUINO_SAMD_NANO_33_IOT)
      #define ARDUINO_TINYUSB_CONSOLE Serial1
    #else
      #define ARDUINO_TINYUSB_CONSOLE Serial
    #endif
  #elif defined(ARDUINO_TINYUSB_BOARD_GCM4) || \
        defined(ARDUINO_TINYUSB_BOARD_GIGA) || \
        defined(ARDUINO_TINYUSB_BOARD_PICO)
    #define ARDUINO_TINYUSB_CONSOLE Serial1
  #else
    #define ARDUINO_TINYUSB_CONSOLE Serial
  #endif
#endif

#if !defined(ARDUINO_TINYUSB_BAUD)
  #define ARDUINO_TINYUSB_BAUD 115200
#endif

// Unified Init API
#ifdef __cplusplus
extern "C" {
#endif

// Role guards: a specialized config (one copied over
// src/tusb_config_arduinotinyusb.h) compiles in only the role it names, so
// the matching entry points do not exist. Guarding the declarations turns
// "built the wrong example against a specialized config" into a compile error
// on the call itself, naming the fix, instead of an undefined reference at
// link time or — worse — a silently empty function body.
#if CFG_TUD_ENABLED
void tud_arduino_init(void);
// Device task pump: call in loop() instead of bare tud_task().
// ESP32 runs FreeRTOS, where bare tud_task() (= tud_task_ext(UINT32_MAX))
// blocks forever on an empty event queue, freezing loop() whenever USB
// idles (observed on hardware: HID enumerates then goes silent, zero
// reports/prints).
// Everywhere else this is a plain tud_task() wrapper.
void tud_arduino_task(void);
#endif // CFG_TUD_ENABLED

#if CFG_TUH_ENABLED
void tuh_arduino_init(void);
// Host task pump: call in loop() instead of bare tuh_task(). Plain wrapper
// on every supported board (on ESP32 it is tuh_task_ext(0, false), since bare
// tuh_task() blocks on the FreeRTOS queue and freezes loop()).
void tuh_arduino_poll(void);
#endif // CFG_TUH_ENABLED

// Role-agnostic: always available, whichever side the config compiles in.
void arduino_tinyusb_probe(void);

#ifdef __cplusplus
}
#endif

// Mode Enum
typedef enum {
  ARDUINO_TINYUSB_MODE_UNINITIALIZED = 0,
  ARDUINO_TINYUSB_MODE_DEVICE,
  ARDUINO_TINYUSB_MODE_HOST
} ArduinoTinyUSBMode;

ArduinoTinyUSBMode arduino_tinyusb_get_mode(void);

// Board-specific extras
#if defined(ARDUINO_TINYUSB_BOARD_DUE)
#ifdef __cplusplus
extern "C" {
#endif
void dcd_sam3x_stats(int *reset, int *setup, int *susp, int *stall, int *setup_w0, int *setup_w1);
void dcd_sam3x_stats2(int *in0, int *out0, int *pending, int *status_done);
void dcd_sam3x_stats3(int *in_submit, int *out_submit, int *kick_txini);
void dcd_sam3x_stats4(int *txini0, int *rxouti0);
void dcd_sam3x_stats5(int *kick_total, int *done_len, int *done_ep);
void dcd_sam3x_ring(int *out16);
void dcd_sam3x_stats6(int *steps, int *staged_total, int *staged_n, int *staged_mps);
void dcd_sam3x_stats7(int *kick, int *rxouti_isr, int *complete, int *no_active, int *no_rxoute, int *cfgok_fail);
void dcd_sam3x_stats8(uint32_t *cfg_out, uint32_t *isr_out, uint32_t *cfg_in, uint32_t *isr_in, uint32_t *devpt, uint32_t *devisr);
void hcd_sam3x_ring(uint32_t *out64);
#ifdef __cplusplus
}
#endif
#endif
