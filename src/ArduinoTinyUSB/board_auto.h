/*
 * board_auto.h — Automatic board detection for Arduino IDE 2.x.
 *
 * Explicit -DARDUINO_TINYUSB_BOARD_* (or a sketch-level #define before any
 * include) always wins. Otherwise the board is inferred from the core's own
 * command-line macros (present in EVERY translation unit, sketch and library
 * alike — unlike sketch #defines, which never reach library sources):
 *
 *   Due      : ARDUINO_SAM_DUE / __SAM3X8E__ / ARDUINO_ARCH_SAM
 *   Zero fam.: ARDUINO_SAMD_ZERO / ARDUINO_SAM_ZERO (M0 Pro) /
 *              ARDUINO_SAMD_NANO_33_IOT / ARDUINO_ARCH_SAMD / __SAMD21G18A__
 *   Giga     : ARDUINO_GIGA / ARDUINO_ARCH_MBED_GIGA
 *              (NOT bare ARDUINO_ARCH_MBED — matches Portenta etc. too)
 *   GCM4     : ADAFRUIT_GRAND_CENTRAL_M4 / __SAMD51P20A__ / __SAMD51__
 *              (adafruit:samd core; must match BEFORE the Zero branch —
 *              both cores define ARDUINO_ARCH_SAMD)
 *   Pico fam.: ARDUINO_ARCH_RP2040 / ARDUINO_RASPBERRY_PI_PICO*
 *   ESP32    : ARDUINO_ARCH_ESP32
 *
 * Uno R4 / Nano R4 (renesas_uno) are deliberately absent and unsupported: an
 * R4 core will therefore match NO board here and fall through to the
 * "Unsupported board" #error in tusb_config_arduinotinyusb.h — a loud
 * failure, not a silent one.
 *
 * Verified against `arduino-cli compile --verbose` -D lines for the Tier A
 * board keys (giga/pico/picow/pico2/pico2w/s2/s3) and the Tier B keys
 * (due/zero/nano33iot/m0pro/gcm4).
 *
 * Standalone: no Arduino.h / tusb dependency, safe from C and C++,
 * safe to include before anything else.
 */

#ifndef ARDUINO_TINYUSB_BOARD_AUTO_H
#define ARDUINO_TINYUSB_BOARD_AUTO_H

#if !defined(ARDUINO_TINYUSB_BOARD_DUE) && \
    !defined(ARDUINO_TINYUSB_BOARD_ZERO) && \
    !defined(ARDUINO_TINYUSB_BOARD_GCM4) && \
    !defined(ARDUINO_TINYUSB_BOARD_GIGA) && \
    !defined(ARDUINO_TINYUSB_BOARD_PICO) && \
    !defined(ARDUINO_TINYUSB_BOARD_ESP32)

#if defined(ARDUINO_SAM_DUE) || defined(__SAM3X8E__) || \
    defined(ARDUINO_ARCH_SAM)
#define ARDUINO_TINYUSB_BOARD_DUE 1
// GCM4 (SAMD51) MUST be checked before Zero: the adafruit:samd core also
// defines ARDUINO_ARCH_SAMD, so a bare arch check below would claim it.
#elif defined(ADAFRUIT_GRAND_CENTRAL_M4) || defined(__SAMD51P20A__) || \
    defined(__SAMD51__)
#define ARDUINO_TINYUSB_BOARD_GCM4 1
#elif defined(ARDUINO_SAMD_ZERO) || defined(ARDUINO_SAM_ZERO) || \
    defined(ARDUINO_SAMD_NANO_33_IOT) || defined(ARDUINO_ARCH_SAMD) || \
    defined(__SAMD21G18A__)
#define ARDUINO_TINYUSB_BOARD_ZERO 1
#elif defined(ARDUINO_GIGA) || defined(ARDUINO_ARCH_MBED_GIGA)
#define ARDUINO_TINYUSB_BOARD_GIGA 1
#elif defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_RASPBERRY_PI_PICO) || \
    defined(ARDUINO_RASPBERRY_PI_PICO_W) || \
    defined(ARDUINO_RASPBERRY_PI_PICO_2) || \
    defined(ARDUINO_RASPBERRY_PI_PICO_2W)
#define ARDUINO_TINYUSB_BOARD_PICO 1
#elif defined(ARDUINO_ARCH_ESP32)
#define ARDUINO_TINYUSB_BOARD_ESP32 1
#endif

#endif /* no explicit board define */

// Exactly one board must be selected after auto-detect.
#if defined(ARDUINO_TINYUSB_BOARD_DUE) + \
        defined(ARDUINO_TINYUSB_BOARD_ZERO) + \
        defined(ARDUINO_TINYUSB_BOARD_GCM4) + \
        defined(ARDUINO_TINYUSB_BOARD_GIGA) + \
        defined(ARDUINO_TINYUSB_BOARD_PICO) + \
        defined(ARDUINO_TINYUSB_BOARD_ESP32) > 1
#error "Define exactly ONE ARDUINO_TINYUSB_BOARD_*"
#endif

#endif /* ARDUINO_TINYUSB_BOARD_AUTO_H */
