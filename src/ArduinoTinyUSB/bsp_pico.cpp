// bsp_pico.cpp — TinyUSB BSP glue for Raspberry Pi Pico (RP2040) / Pico 2 (RP2350).
//
// Unlike Zero, there is no clock/GPIO/IRQ work here:
//   - rp2usb_init() (portable/raspberrypi/rp2040/rp2040_usb.c) does the
//     USBCTRL reset + PHY mux itself from dcd_init()/hcd_init().
//   - The port driver self-registers its USBCTRL_IRQ shared handler
//     (dcd_rp2040.c / hcd_rp2040.c); the core registers nothing under
//     usbstack=nousb (SerialUSB.cpp is compiled out), so no ISR override.
//   - The core auto-starts Serial1 (DEBUG_RP2040_PORT.begin(115200) in
//     main.cpp runs outside the NO_USB guard), so no Serial1.begin() here.
// Single native controller: device XOR host at runtime (same model as Zero).

#include "board_auto.h"
#if defined(ARDUINO_TINYUSB_BOARD_PICO)

#include "Arduino.h"
#include "../ArduinoTinyUSB.h"
#include "bsp_common.h"
#include "hardware/structs/usb.h"

#define PICO_RHPORT 0

// Mode tracking (Pico is single-port, device XOR host at runtime)
static ArduinoTinyUSBMode _pico_mode = ARDUINO_TINYUSB_MODE_UNINITIALIZED;

void tud_arduino_init(void) {
#if CFG_TUD_ENABLED
  // NOTE: Do NOT enable USBCTRL_IRQ here — tud_rhport_init() → dcd_init()
  // configures device mode, then dcd_int_enable() enables the IRQ safely.
  _pico_mode = ARDUINO_TINYUSB_MODE_DEVICE;
  const tusb_rhport_init_t tud_init_cfg = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_FULL
  };
  tud_rhport_init(PICO_RHPORT, &tud_init_cfg);
#endif
}

void tuh_arduino_init(void) {
#if CFG_TUH_ENABLED
  // NOTE: Do NOT enable USBCTRL_IRQ here — tuh_rhport_init() → hcd_init()
  // configures host mode, then hcd_int_enable() enables the IRQ safely.
  _pico_mode = ARDUINO_TINYUSB_MODE_HOST;
  const tusb_rhport_init_t tuh_init_cfg = {
    .role = TUSB_ROLE_HOST,
    .speed = TUSB_SPEED_FULL,
  };
  tuh_rhport_init(PICO_RHPORT, &tuh_init_cfg);
#endif
}

// Host task pump (plain tuh_task() wrapper; see bsp_due.cpp note).
void tuh_arduino_poll(void) {
#if CFG_TUH_ENABLED
  tuh_task();
#endif
}

void arduino_tinyusb_probe(void) {
  char line[160];
  snprintf(line, sizeof(line),
    "[PICO] probe MAIN_CTRL=%08lx SIE_STATUS=%08lx INTS=%08lx",
    (unsigned long)usb_hw->main_ctrl, (unsigned long)usb_hw->sie_status,
    (unsigned long)usb_hw->ints);
  ARDUINO_TINYUSB_CONSOLE.println(line);
  snprintf(line, sizeof(line),
    "[PICO] probe SIE_CTRL=%08lx INTE=%08lx PWR=%08lx MUXING=%08lx",
    (unsigned long)usb_hw->sie_ctrl, (unsigned long)usb_hw->inte,
    (unsigned long)usb_hw->pwr, (unsigned long)usb_hw->muxing);
  ARDUINO_TINYUSB_CONSOLE.println(line);
}

// Mode query (state set by tud/tuh_arduino_init above)
ArduinoTinyUSBMode arduino_tinyusb_get_mode(void) { return _pico_mode; }

#endif // ARDUINO_TINYUSB_BOARD_PICO
