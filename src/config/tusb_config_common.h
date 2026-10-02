/*
 * tusb_config_common.h — shared base for per-example TinyUSB configs.
 *
 * NOT included directly (selected via -DCFG_TUSB_CONFIG_FILE="config/<sig>.h").
 * Each signature header (this directory) defines CFG_TUD_ENABLED /
 * CFG_TUH_ENABLED plus its class counts FIRST, then includes this file,
 * so the `#if CFG_TUD/TUH_ENABLED` size blocks below resolve correctly.
 *
 * Board/MCU/OS/queue/debug sections are verbatim from
 * src/tusb_config_arduinotinyusb.h (the unified fallback).
 */

#include "ArduinoTinyUSB/board_auto.h"

#ifndef ARDUINO_TINYUSB_TUSB_CONFIG_COMMON_H
#define ARDUINO_TINYUSB_TUSB_CONFIG_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

// Board Selection Validation (explicit -D or board_auto.h must have fired)
// NOTE: this board list is a SECOND copy — the first is in
// src/tusb_config_arduinotinyusb.h (the unified fallback). A board added to
// one but not the other builds fine on the unified path and fails HERE with
// "Unsupported board" on the per-example path. Keep them in sync.
#if !defined(ARDUINO_TINYUSB_BOARD_DUE) && !defined(ARDUINO_TINYUSB_BOARD_ZERO) && \
    !defined(ARDUINO_TINYUSB_BOARD_GCM4) && !defined(ARDUINO_TINYUSB_BOARD_GIGA) && \
    !defined(ARDUINO_TINYUSB_BOARD_PICO) && \
    !defined(ARDUINO_TINYUSB_BOARD_ESP32)
  #error "Unsupported board: no ARDUINO_TINYUSB_BOARD_* detected (define one explicitly)"
#endif

// MCU Selection
#if defined(ARDUINO_TINYUSB_BOARD_DUE)
  #define CFG_TUSB_MCU          OPT_MCU_SAM3X
  #define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#elif defined(ARDUINO_TINYUSB_BOARD_ZERO)
  #define CFG_TUSB_MCU          OPT_MCU_SAMD21
  #define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#elif defined(ARDUINO_TINYUSB_BOARD_GCM4)
  // Grand Central M4 (SAMD51): same SAMD port as Zero — the vendored
  // dcd_samd.c/hcd_samd.c have explicit SAMD51 branches (4-line USB IRQ,
  // MCLK masks). Single port, FS only, device XOR host at runtime, identical
  // rhport-0 config to Zero. (OPT_MODE_HOST is deliberately not OR'd in:
  // CFG_TUH_ENABLED comes from the signature header, and the bit would only
  // shift the CFG_TUH_MAX_SPEED fallback, dropping TUH_EPSIZE_BULK_MAX to 64
  // vs the proven SAMD21 host stack.)
  #define CFG_TUSB_MCU          OPT_MCU_SAMD51
  #define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#elif defined(ARDUINO_TINYUSB_BOARD_GIGA)
  #define CFG_TUSB_MCU          OPT_MCU_STM32H7
  // Giga: device on rhport 0 (OTG_FS, Type C), host on rhport 1 (OTG_HS, Type A).
  // Per-example role selection is via CFG_TUD/TUH_ENABLED (set by the
  // signature header); the idle port is simply never initialized.
  #define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
  #define CFG_TUSB_RHPORT1_MODE (OPT_MODE_HOST   | OPT_MODE_FULL_SPEED)
#elif defined(ARDUINO_TINYUSB_BOARD_PICO)
  #define CFG_TUSB_MCU          OPT_MCU_RP2040
  #define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
  // Native controller only (hcd/dcd gate on !CFG_TUx_RPI_PIO_USB already,
  // but lock it explicitly so PIO-USB can never sneak in via core flags).
  #define CFG_TUD_RPI_PIO_USB   0
  #define CFG_TUH_RPI_PIO_USB   0
#elif defined(ARDUINO_TINYUSB_BOARD_ESP32)
  #if CONFIG_IDF_TARGET_ESP32S2
    #define CFG_TUSB_MCU          OPT_MCU_ESP32S2
  #else
    #define CFG_TUSB_MCU          OPT_MCU_ESP32S3
  #endif
  #define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_HOST | OPT_MODE_FULL_SPEED)
#endif

#ifndef CFG_TUSB_OS
  // ESP32 Arduino runs FreeRTOS — the stack must use the FreeRTOS osal.
  // The rp2040 core pre-defines CFG_TUSB_OS=OPT_OS_PICO via
  // lib/platform_def.txt (vendored osal_pico.h uses pico-sdk locks);
  // keep it there. Everywhere else default to OPT_OS_NONE.
  #if defined(ARDUINO_TINYUSB_BOARD_ESP32)
    #define CFG_TUSB_OS         OPT_OS_FREERTOS
  #else
    #define CFG_TUSB_OS         OPT_OS_NONE
  #endif
#endif

// Deep event queues (do NOT trim): with CFG_TUSB_DEBUG=2 console logging
// (~5-10ms per control transfer at 115200 baud), bursty EP0 traffic during
// enumeration overflows the default 16-deep queue, silently dropping
// SETUP/xfer-complete events (observed on hardware with a Due device and a
// Zero host).
// 64 entries x ~12 bytes is affordable on all boards.
#define CFG_TUD_TASK_QUEUE_SZ   64
#define CFG_TUH_TASK_QUEUE_SZ   64

#if CFG_TUD_ENABLED
// Device Buffer Sizes
#define CFG_TUD_CDC_RX_BUFSIZE   256
#define CFG_TUD_CDC_TX_BUFSIZE   256
#define CFG_TUD_MSC_BUFSIZE      512
#define CFG_TUD_MIDI_RX_BUFSIZE  256
#define CFG_TUD_MIDI_TX_BUFSIZE  256
#define CFG_TUD_PRINTER_RX_BUFSIZE 256
#define CFG_TUD_PRINTER_TX_BUFSIZE 256
#endif

// Audio Device Config (macros only — zero cost unless CFG_TUD_AUDIO > 0)
#define CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE            48000
#define CFG_TUD_AUDIO_ENABLE_EP_IN                  1
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX  2
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX          1
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX  TUD_AUDIO_EP_SIZE(0, 48000, 2, 1)
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX TUD_AUDIO_EP_SIZE(0, 48000, 2, 1)
#define CFG_TUD_AUDIO_FUNC_2_EP_IN_SZ_MAX  TUD_AUDIO_EP_SIZE(0, 48000, 2, 1)
#define CFG_TUD_AUDIO_FUNC_2_EP_OUT_SZ_MAX TUD_AUDIO_EP_SIZE(0, 48000, 2, 1)
#define CFG_TUD_AUDIO_FUNC_3_EP_IN_SZ_MAX  TUD_AUDIO_EP_SIZE(0, 48000, 2, 1)
#define CFG_TUD_AUDIO_FUNC_3_EP_OUT_SZ_MAX TUD_AUDIO_EP_SIZE(0, 48000, 2, 1)
#define CFG_TUD_AUDIO_EP_SZ_IN  TUD_AUDIO_EP_SIZE(0, 48000, 2, 1)
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ  (4 * CFG_TUD_AUDIO_EP_SZ_IN)
#define CFG_TUD_AUDIO_FUNC_2_EP_IN_SW_BUF_SZ  (4 * CFG_TUD_AUDIO_EP_SZ_IN)
#define CFG_TUD_AUDIO_FUNC_3_EP_IN_SW_BUF_SZ  (4 * CFG_TUD_AUDIO_EP_SZ_IN)

#if CFG_TUH_ENABLED
// Host Buffer Sizes (board-specific tuning)
#if defined(ARDUINO_TINYUSB_BOARD_GIGA)
  // Giga: larger FIFOs for UART bridge throughput
  #define CFG_TUH_CDC_RX_BUFSIZE   1024
  #define CFG_TUH_CDC_TX_BUFSIZE   1024
  #define CFG_TUH_MIDI_RX_BUFSIZE  1024
  #define CFG_TUH_MIDI_TX_BUFSIZE  1024
  #define CFG_TUH_DEVICE_MAX       5
  #define CFG_TUH_MSC_MAXLUN       1
  #define CFG_TUH_MSC_EP_BUFSIZE   512
  #define CFG_TUH_HUB_BUFSIZE      64
  #define CFG_TUH_CDC_LINE_CODING_ON_ENUM {115200, CDC_LINE_CODING_STOP_BITS_1, CDC_LINE_CODING_PARITY_NONE, 8}
#else
  // Due/Zero/Pico/ESP32: default 256-byte FIFOs
  #define CFG_TUH_CDC_RX_BUFSIZE   256
  #define CFG_TUH_CDC_TX_BUFSIZE   256
  #define CFG_TUH_MIDI_RX_BUFSIZE  256
  #define CFG_TUH_MIDI_TX_BUFSIZE  256
#endif

// Host Audio Config (macros only — zero cost unless CFG_TUH_AUDIO > 0)
#define CFG_TUH_AUDIO_PROTOCOLS  (TUH_AUDIO_PROTOCOL_UAC1 | TUH_AUDIO_PROTOCOL_UAC2)
#define CFG_TUH_AUDIO_MAX        1
#define CFG_TUH_AUDIO_MAX_SAM_FREQ 5
#define CFG_TUH_AUDIO_MAX_AS     2
#define CFG_TUH_AUDIO_EPIN_BUFSIZE  256
#define CFG_TUH_AUDIO_STREAM_BUFSIZE 1024
#endif // CFG_TUH_ENABLED

// Debug: #ifndef-guarded so release builds can pass -DCFG_TUSB_DEBUG=0
// (upstream TinyUSB default) without editing files.
#ifndef CFG_TUSB_DEBUG
  #define CFG_TUSB_DEBUG           2
#endif
#define CFG_TUSB_DEBUG_PRINTF    arduino_debug_printf

#ifdef __cplusplus
}
#endif

#endif // ARDUINO_TINYUSB_TUSB_CONFIG_COMMON_H

