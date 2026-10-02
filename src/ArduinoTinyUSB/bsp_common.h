/*
 * Arduino-TinyUSB Common BSP Header
 *
 * Shared macros and declarations across all boards.
 */

#ifndef ARDUINO_TINYUSB_BSP_COMMON_H
#define ARDUINO_TINYUSB_BSP_COMMON_H

// Board auto-detect (IDE 2.x) before any ARDUINO_TINYUSB_BOARD_* use below.
#include "board_auto.h"

#include <Arduino.h>

// SAM core USBCore.h defines these as macros, conflicting with TinyUSB enums
#undef MSC_SUBCLASS_SCSI
#undef MSC_PROTOCOL_BULK_ONLY

#include <stdarg.h>

// ─── Console Defaults ───
// Due: Serial = UART programming port (ATmega16U2 bridge), safe to use.
// Zero/M0 Pro: Serial = EDBG SERCOM5 UART, independent of the native USB
//   peripheral. Verified on hardware: host mode on native USB + a
//   Serial.print heartbeat on the programming port run concurrently.
//   TinyUSB's USB_Handler only touches USB_IRQn, never
//   SERCOM5_IRQn, so the same holds in TinyUSB mode — no FTDI bridge needed.
// Nano 33 IoT: Serial == SerialUSB (native, see variant.h), owned by
//   TinyUSB, so default to Serial1 (UART pins).
// GIGA (host on Type A): Serial = Mbed CDC on Type C is safe for host
//   examples, but device examples take over OTG_FS (Type C) — keep Serial1
//   as the unified default.
// GCM4: no EDBG/second USB port; Serial = native USB CDC (owned by TinyUSB
//   in device mode), so console = Serial1 (SERCOM0 on D0/D1, needs an
//   external UART bridge — there is no USB-serial chip on this board).
// Pico (usbstack=nousb): Serial (SerialUSB) doesn't exist; Serial1 (UART0 on
//   GP0/GP1) is auto-started by the core (DEBUG_RP2040_PORT) — use Serial1.
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

// ─── NVIC Priority (avoid starving UART) ───
#define ARDUINO_TINYUSB_NVIC_PRIO 5UL

// ─── Debug Printf ───
#ifdef __cplusplus
extern "C" {
#endif

int arduino_debug_printf(char const *format, ...);

#ifdef __cplusplus
}
#endif

#endif // ARDUINO_TINYUSB_BSP_COMMON_H
