/*
 * Arduino-TinyUSB Unified Configuration
 *
 * Board selection: automatic via ArduinoTinyUSB/board_auto.h (core macros),
 * or define exactly ONE of these for an explicit override:
 *   -DARDUINO_TINYUSB_BOARD_DUE       (Tier B, compile-only)
 *   -DARDUINO_TINYUSB_BOARD_ZERO      (Tier B, compile-only)
 *   -DARDUINO_TINYUSB_BOARD_GCM4      (Tier B, compile-only)
 *   -DARDUINO_TINYUSB_BOARD_GIGA
 *   -DARDUINO_TINYUSB_BOARD_PICO
 *   -DARDUINO_TINYUSB_BOARD_ESP32
 */

#include "ArduinoTinyUSB/board_auto.h"

#ifndef ARDUINO_TINYUSB_TUSB_CONFIG_H
#define ARDUINO_TINYUSB_TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// Board Selection Validation (explicit -D or board_auto.h must have fired)
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
  // dcd_samd.c/hcd_samd.c have explicit SAMD51 branches (4-line USB IRQ
  // USB_0..3, MCLK masks instead of PM). Single port, FS only, device XOR
  // host at runtime — identical rhport-0 config to Zero above. (OPT_MODE_HOST
  // is deliberately not OR'd in: CFG_TUH_ENABLED is set unconditionally a few
  // lines down, so the bit only shifts the CFG_TUH_MAX_SPEED fallback, which
  // would drop TUH_EPSIZE_BULK_MAX to 64 and break parity with the proven
  // Zero host stack.)
  #define CFG_TUSB_MCU          OPT_MCU_SAMD51
  #define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#elif defined(ARDUINO_TINYUSB_BOARD_GIGA)
  #define CFG_TUSB_MCU          OPT_MCU_STM32H7
  // Giga: device on rhport 0 (OTG_FS, Type C), host on rhport 1 (OTG_HS, Type A)
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
  // ESP32 device-only. S2/S3 split via IDF target macro (S2/P4 bring-up
  // follows the proven S3 path: dwc2_esp32.h already carries all three).
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

// Device Stack (all boards)
#define CFG_TUD_ENABLED         1
// Deep event queue (mirrors CFG_TUH_TASK_QUEUE_SZ below): bursty EP0
// traffic during enumeration overflows the default 16-deep queue,
// silently dropping SETUP/xfer-complete events.
// Observed on hardware: enumeration died right after SET_ADDRESS (device
// ACKed setups in HW but SW never saw them; host saw endless NAKs on DATA
// INs). 64 entries is affordable.
// This depth is unconditional: do NOT trim it back to 16 because
// CFG_TUSB_DEBUG is 0. The ISR latency from console logging is only part
// of why enumeration is bursty — trim it again and if logging is ever
// re-enabled (see CFG_TUSB_DEBUG below) events start going missing again.
#define CFG_TUD_TASK_QUEUE_SZ   64
#define CFG_TUD_CDC             2 // Dual-CDC needs 2 (cdcd_open asserts 2nd iface if 1)
#define CFG_TUD_HID             2
#define CFG_TUD_MIDI            1
#define CFG_TUD_MSC             1
#define CFG_TUD_AUDIO           1
#define CFG_TUD_USBTMC          1
#define CFG_TUD_VENDOR          1
#define CFG_TUD_NCM             1
#define CFG_TUD_DFU_RUNTIME     1
#define CFG_TUD_PRINTER         1
#define CFG_TUD_BTH             1
#define CFG_TUD_BTH_ISO_ALT_COUNT 2
#define CFG_TUD_VIDEO           0
#define CFG_TUD_ECM_RNDIS       0
#define CFG_TUD_MTP             0

// Host Stack (all boards)
#define CFG_TUH_ENABLED         1
// Deep event queue: bursty EP0 traffic (8-byte packets for large
// MIDI/audio descriptors) overflows the default 16-deep queue,
// dropping xfer-complete events and corrupting enumeration
// ("USBH event limit (16) reached" + cascade of STALL/FAILED).
// 64 entries x ~12 bytes is affordable on all boards.
// Unconditional — do NOT trim to 16 just because CFG_TUSB_DEBUG is 0.
// See the CFG_TUD_TASK_QUEUE_SZ comment above.
#define CFG_TUH_TASK_QUEUE_SZ   64
#define CFG_TUH_CDC             1
#define CFG_TUH_CDC_FTDI        1
#define CFG_TUH_CDC_PL2303      1
#define CFG_TUH_CDC_CP210X      1
#define CFG_TUH_CDC_CH34X       1
#define CFG_TUH_MIDI            1
#define CFG_TUH_HID             4
#define CFG_TUH_MSC             1
#define CFG_TUH_HUB             1
#define CFG_TUH_PRINTER         1
#define CFG_TUH_AUDIO           1

// Host Audio Config (all boards)
#define CFG_TUH_AUDIO_PROTOCOLS  (TUH_AUDIO_PROTOCOL_UAC1 | TUH_AUDIO_PROTOCOL_UAC2)
#define CFG_TUH_AUDIO_MAX        1
#define CFG_TUH_AUDIO_MAX_SAM_FREQ 5
#define CFG_TUH_AUDIO_MAX_AS     2
#define CFG_TUH_AUDIO_EPIN_BUFSIZE  256
#define CFG_TUH_AUDIO_STREAM_BUFSIZE 1024

// Device Buffer Sizes (all boards)
#define CFG_TUD_CDC_RX_BUFSIZE   256
#define CFG_TUD_CDC_TX_BUFSIZE   256
#define CFG_TUD_MSC_BUFSIZE      512
#define CFG_TUD_MIDI_RX_BUFSIZE  256
#define CFG_TUD_MIDI_TX_BUFSIZE  256
#define CFG_TUD_PRINTER_RX_BUFSIZE 256
#define CFG_TUD_PRINTER_TX_BUFSIZE 256

// Audio Device Config (all boards)
#define CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE            48000
#define CFG_TUD_AUDIO_ENABLE_EP_IN                  1
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX  2
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX          1
// EP IN/OUT max sizes (required by audio_device.h)
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
  // Due/Zero/Pico: default 256-byte FIFOs
  #define CFG_TUH_CDC_RX_BUFSIZE   256
  #define CFG_TUH_CDC_TX_BUFSIZE   256
  #define CFG_TUH_MIDI_RX_BUFSIZE  256
  #define CFG_TUH_MIDI_TX_BUFSIZE  256
#endif

// Debug off by default (upstream TinyUSB default): TU_LOG runs in the USB
// ISR, so a ~40-char line at 115200 baud blocks for ~3.5 ms -- longer than
// a 1 ms isochronous audio frame. Keep it 0 for audio/streaming work.
// #ifndef-guarded, so -DCFG_TUSB_DEBUG=2 re-enables logging for bring-up
// without editing files. CFG_TUSB_DEBUG_PRINTF stays outside the guard:
// arduino_debug_printf() (src/ArduinoTinyUSB/bsp_common.cpp) must resolve
// even while logging is off, or the opt-in fails at link.
#ifndef CFG_TUSB_DEBUG
  #define CFG_TUSB_DEBUG         0
#endif
#define CFG_TUSB_DEBUG_PRINTF    arduino_debug_printf

#ifdef __cplusplus
}
#endif

#endif // ARDUINO_TINYUSB_TUSB_CONFIG_H
