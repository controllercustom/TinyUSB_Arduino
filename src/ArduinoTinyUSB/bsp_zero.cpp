// bsp_zero.cpp — TinyUSB BSP glue for Arduino Zero (SAMD21G18A USBDevice).
//
// Hardware init recipe lifted from the SAMD core (cores/arduino/USB/USBCore.cpp
// USB.begin()):
//   PM->APBBMASK USB (peripheral clock), GCLK6 <- GCLK0 (48 MHz DFLL48M,
//   already set by startup.c), PMUX PA24/PA25 to USB DM/DP.
// The upstream TinyUSB SAMD port (dcd_samd.c / hcd_samd.c) does the rest:
// SWRST, PADCAL from fuses, QoS, device/host mode.
// IRQ: the core's USB_Handler (cortex_handlers.c) is weak; our strong
// definition below overrides it and dispatches to the active port driver.
// The core's USB_SetHandler() would clobber this, but only if the sketch
// calls USB.begin()/SerialUSB.begin(), which is forbidden in TinyUSB mode.

#include "board_auto.h"
#if defined(ARDUINO_TINYUSB_BOARD_ZERO)

#include "Arduino.h"
#include "../ArduinoTinyUSB.h"
#include "bsp_common.h"

// TinyUSB port drivers (src/portable/microchip/samd/).
extern "C" void dcd_int_handler(uint8_t rhport);
extern "C" void hcd_int_handler(uint8_t rhport, bool in_isr);

#define ZERO_RHPORT 0

static volatile bool s_host_mode = false;

// Strong override of the core's weak USB_Handler (USB_IRQn, vector 7).
extern "C" void USB_Handler(void) {
#if CFG_TUH_ENABLED
  if (s_host_mode) {
    hcd_int_handler(ZERO_RHPORT, true);
    return;
  }
#endif
#if CFG_TUD_ENABLED
  dcd_int_handler(ZERO_RHPORT);
#endif
}

static void zero_tusb_clocks_init(void) {
  // USB peripheral clock (APBB).
  PM->APBBMASK.reg |= PM_APBBMASK_USB;
  // USB reference = GCLK6 <- GCLK0. GCLK0 is DFLL48M (48 MHz), locked in by
  // the core startup.c before main(); USB requires exactly 48 MHz.
  GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(6) | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_CLKEN;
  while (GCLK->STATUS.bit.SYNCBUSY)
    ;
  // PA24 = USB DM, PA25 = USB DP (PMUX G) — same recipe as core USBCore.cpp.
  PORT->Group[0].PINCFG[PIN_PA24G_USB_DM].bit.PMUXEN = 1;
  PORT->Group[0].PMUX[PIN_PA24G_USB_DM / 2].reg &= ~(0xF << (4 * (PIN_PA24G_USB_DM & 0x01u)));
  PORT->Group[0].PMUX[PIN_PA24G_USB_DM / 2].reg |= MUX_PA24G_USB_DM << (4 * (PIN_PA24G_USB_DM & 0x01u));
  PORT->Group[0].PINCFG[PIN_PA25G_USB_DP].bit.PMUXEN = 1;
  PORT->Group[0].PMUX[PIN_PA25G_USB_DP / 2].reg &= ~(0xF << (4 * (PIN_PA25G_USB_DP & 0x01u)));
  PORT->Group[0].PMUX[PIN_PA25G_USB_DP / 2].reg |= MUX_PA25G_USB_DP << (4 * (PIN_PA25G_USB_DP & 0x01u));
}

void tud_arduino_init(void) {
#if CFG_TUD_ENABLED
  ARDUINO_TINYUSB_CONSOLE.println("[TUD] step: clocks...");
  zero_tusb_clocks_init();
  s_host_mode = false;
  // NOTE: Do NOT enable USB_IRQn here — same reason as tuh_arduino_init().
  // tud_rhport_init() → dcd_init() configures device mode, then
  // dcd_int_enable() enables the IRQ safely.
  ARDUINO_TINYUSB_CONSOLE.println("[TUD] step: tud_rhport_init...");
  const tusb_rhport_init_t tud_init_cfg = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_FULL
  };
  tud_rhport_init(ZERO_RHPORT, &tud_init_cfg);
  ARDUINO_TINYUSB_CONSOLE.println("[TUD] init done");
#endif
}

void tuh_arduino_init(void) {
#if CFG_TUH_ENABLED
  ARDUINO_TINYUSB_CONSOLE.println("[TUH] step: clocks...");
  zero_tusb_clocks_init();
  s_host_mode = true;
  // NOTE: Do NOT enable USB_IRQn here — the bootloader may leave the
  // USB controller in device mode with pending interrupts. Enabling the
  // IRQ before tuh_rhport_init() causes our strong USB_Handler to call
  // hcd_int_handler() on a device-mode controller → HardFault.
  // tuh_rhport_init() → hcd_init() configures host mode, then
  // hcd_int_enable() enables the IRQ safely.
  ARDUINO_TINYUSB_CONSOLE.println("[TUH] step: tuh_rhport_init...");
  const tusb_rhport_init_t tuh_init_cfg = {
    .role = TUSB_ROLE_HOST,
    .speed = TUSB_SPEED_FULL,
  };
  tuh_rhport_init(ZERO_RHPORT, &tuh_init_cfg);
  ARDUINO_TINYUSB_CONSOLE.println("[TUH] init done");
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
    "[ZERO] probe APBBMASK_USB=%lu CLKCTRL=%02lx(CLKEN=%lu)",
    (unsigned long)((PM->APBBMASK.reg & PM_APBBMASK_USB) ? 1 : 0),
    (unsigned long)GCLK->CLKCTRL.reg,
    (unsigned long)GCLK->CLKCTRL.bit.CLKEN);
  ARDUINO_TINYUSB_CONSOLE.println(line);
  snprintf(line, sizeof(line),
    "[ZERO] probe USB CTRLA=%08lx CTRLB=%08lx INTFLAG=%08lx INTENSET=%08lx",
    (unsigned long)USB->DEVICE.CTRLA.reg, (unsigned long)USB->DEVICE.CTRLB.reg,
    (unsigned long)USB->DEVICE.INTFLAG.reg, (unsigned long)USB->DEVICE.INTENSET.reg);
  ARDUINO_TINYUSB_CONSOLE.println(line);
  snprintf(line, sizeof(line),
    "[ZERO] probe DADD=%02lx FSM=%02lx EPINTSMRY=%04lx",
    (unsigned long)USB->DEVICE.DADD.reg,
    (unsigned long)USB->DEVICE.FSMSTATUS.reg,
    (unsigned long)USB->DEVICE.EPINTSMRY.reg);
  ARDUINO_TINYUSB_CONSOLE.println(line);
  snprintf(line, sizeof(line),
    "[ZERO] probe EP0 CFG=%02lx STATUS=%02lx INTFLAG=%02lx INTENSET=%02lx",
    (unsigned long)USB->DEVICE.DeviceEndpoint[0].EPCFG.reg,
    (unsigned long)USB->DEVICE.DeviceEndpoint[0].EPSTATUS.reg,
    (unsigned long)USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.reg,
    (unsigned long)USB->DEVICE.DeviceEndpoint[0].EPINTENSET.reg);
  ARDUINO_TINYUSB_CONSOLE.println(line);
}

// Mode tracking (Zero is single-port, device XOR host at runtime)
ArduinoTinyUSBMode _zero_mode = ARDUINO_TINYUSB_MODE_UNINITIALIZED;
ArduinoTinyUSBMode arduino_tinyusb_get_mode(void) { return _zero_mode; }

#endif // ARDUINO_TINYUSB_BOARD_ZERO
