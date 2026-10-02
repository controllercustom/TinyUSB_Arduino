// bsp_gcm4.cpp — TinyUSB BSP glue for Adafruit Grand Central M4 Express
// (SAMD51P20A native USB).
//
// Hardware init recipe lifted from the Adafruit SAMD core (core 1.7.17,
// cores/arduino/USB/USBCore.cpp, __SAMD51__ branch of USB.begin()):
//   MCLK->APBBMASK + MCLK->AHBMASK for USB, PA24/PA25 PMUX (H mux),
//   USB peripheral clock = Generic Clock Generator 1 (48 MHz, locked in by
//   core startup.c before main() — GENERIC_CLOCK_GENERATOR_48M = 1).
// The vendored TinyUSB SAMD port (dcd_samd.c / hcd_samd.c) has explicit
// SAMD51 branches for the rest (SWRST, PADCAL, QoS, device/host mode).
//
// IRQ difference vs Zero: SAMD51 splits the USB peripheral into FOUR IRQ
// lines (USB_0..3_IRQn = 80..83; dcd_int_enable in the port enables all
// four, and they all share the same INTFLAG/INTENSET). The adafruit core
// forward-declares USB_0..3_Handler __attribute__((weak)) at
// cores/arduino/cortex_handlers.c:132-135 and defines them further down to
// funnel into a USB_SetHandler() function pointer — so our four strong
// definitions below override them and the USB_SetHandler() indirection is
// bypassed entirely. (Belt-and-braces vs bsp_zero.cpp, which overrides a
// single weak USB_Handler: four lines, one dispatch body each.) Using
// strong overrides instead of USB_SetHandler() also means a sketch that
// accidentally calls USB.begin()/SerialUSB.begin() — reachable here because
// the FQBN default is usbstack=arduino, so USBCore.cpp *is* compiled —
// cannot silently steal the dispatch by re-registering UDD_Handler.
//
// Host mode: USB host enable = PA27 (Arduino pin 77, PIN_USB_HOST_ENABLE).
// SAMD51 cannot source 5 V VBUS in host mode — the downstream side must be
// powered externally (VBUS-injection cable), and the board itself must not
// be powered through the USB port while running in host mode.

#include "board_auto.h"
#if defined(ARDUINO_TINYUSB_BOARD_GCM4)

#include "Arduino.h"
#include "../ArduinoTinyUSB.h"
#include "bsp_common.h"

// ─── Core pre-setup USB hook (REQUIRED, see below) ───
// The adafruit:samd core's main.cpp runs, before setup():
//     #if   defined(USE_TINYUSB) TinyUSB_Device_Init(0);
//     #elif defined(USBCON)     { USBDevice.init(); USBDevice.attach(); }
//     #endif
// The GCM4 board defines USBCON at board level, so the default
// `usbstack=arduino` menu would have the core's Arduino USB stack reset and
// enable the native-USB peripheral — and lift USB_0..3_IRQn at priority 0 —
// before our stack exists. Our strong USB_n_Handler would then service core
// state (UDD_Handler is installed through USB_SetHandler, which we bypass)
// against a half-initialised _usbd. So the GCM4 key builds with
// `usbstack=tinyusb` (-DUSE_TINYUSB), which compiles USBCore.cpp out
// entirely and routes main() to TinyUSB_Device_Init() instead.
// That symbol is only declared __attribute__((weak)) (bundled
// Adafruit_TinyUSB_API.h, pulled in by Arduino.h under USE_TINYUSB) and
// defined in the core's bundled Adafruit_TinyUSB_Arduino library, which we
// deliberately do NOT compile (it would duplicate the whole vendored stack).
// Unresolved weak => resolves to NULL => main() jumps to 0 before setup().
// Defining it here (strong beats weak) as a deliberate no-op keeps the single
// init point every example in this library uses: an explicit
// tud_arduino_init() / tuh_arduino_init() call from setup().
extern "C" void TinyUSB_Device_Init(uint8_t rhport) {
  (void) rhport; // no-op: examples init the stack explicitly in setup()
}

// TinyUSB port drivers (src/portable/microchip/samd/, SAMD51 branches).
// Declarations are conditional to match the per-example config mechanism: a
// device-only example sets CFG_TUH_ENABLED 0, and usbh.h (hence
// hcd_int_handler / tuh_rhport_init / tuh_task) is not included at all.
#if CFG_TUH_ENABLED
extern "C" void hcd_int_handler(uint8_t rhport, bool in_isr);
#endif
#if CFG_TUD_ENABLED
extern "C" void dcd_int_handler(uint8_t rhport);
#endif

#define GCM4_RHPORT 0

static volatile bool s_host_mode = false;

// Mode tracking (GCM4 is single-port, device XOR host at runtime)
static ArduinoTinyUSBMode _gcm4_mode = ARDUINO_TINYUSB_MODE_UNINITIALIZED;

static void gcm4_usb_dispatch(void) {
#if CFG_TUH_ENABLED
  if (s_host_mode) {
    hcd_int_handler(GCM4_RHPORT, true);
    return;
  }
#endif
#if CFG_TUD_ENABLED
  dcd_int_handler(GCM4_RHPORT);
#endif
}

// Strong overrides of the core's four weak USB_0..3_Handler stubs. All four
// SAMD51 USB IRQ lines feed the same peripheral INTFLAG/INTENSET register,
// so each one gets the same dispatch.
extern "C" void USB_0_Handler(void) { gcm4_usb_dispatch(); }
extern "C" void USB_1_Handler(void) { gcm4_usb_dispatch(); }
extern "C" void USB_2_Handler(void) { gcm4_usb_dispatch(); }
extern "C" void USB_3_Handler(void) { gcm4_usb_dispatch(); }

static void gcm4_tusb_clocks_init(void) {
  // USB peripheral clocks (AHB + APB). SAMD51 has no PM APBBMASK — use MCLK.
  MCLK->APBBMASK.reg |= MCLK_APBBMASK_USB;
  MCLK->AHBMASK.reg  |= MCLK_AHBMASK_USB;
  // USB reference = Generic Clock Generator 1 (48 MHz from core startup).
  // USB_GCLK_ID is 10 on SAMD51.
  GCLK->PCHCTRL[USB_GCLK_ID].reg =
      GCLK_PCHCTRL_GEN(GCLK_PCHCTRL_GEN_GCLK1_Val) | (1u << GCLK_PCHCTRL_CHEN_Pos);
  // PA24 = USB DM, PA25 = USB DP (H mux) — same recipe as core USBCore.cpp.
  PORT->Group[0].PINCFG[PIN_PA24H_USB_DM].bit.PMUXEN = 1;
  PORT->Group[0].PMUX[PIN_PA24H_USB_DM / 2].reg &= ~(0xF << (4 * (PIN_PA24H_USB_DM & 0x01u)));
  PORT->Group[0].PMUX[PIN_PA24H_USB_DM / 2].reg |= MUX_PA24H_USB_DM << (4 * (PIN_PA24H_USB_DM & 0x01u));
  PORT->Group[0].PINCFG[PIN_PA25H_USB_DP].bit.PMUXEN = 1;
  PORT->Group[0].PMUX[PIN_PA25H_USB_DP / 2].reg &= ~(0xF << (4 * (PIN_PA25H_USB_DP & 0x01u)));
  PORT->Group[0].PMUX[PIN_PA25H_USB_DP / 2].reg |= MUX_PA25H_USB_DP << (4 * (PIN_PA25H_USB_DP & 0x01u));
}

void tud_arduino_init(void) {
#if CFG_TUD_ENABLED
  ARDUINO_TINYUSB_CONSOLE.println("[TUD] step: clocks...");
  gcm4_tusb_clocks_init();
  s_host_mode = false;
  // NOTE: Do NOT enable USB_0..3_IRQn here — the bootloader may leave the
  // controller in device mode with pending interrupts. tud_rhport_init() →
  // dcd_init() configures device mode, then dcd_int_enable() (port, SAMD51
  // branch) lifts all four IRQ lines safely.
  _gcm4_mode = ARDUINO_TINYUSB_MODE_DEVICE;
  ARDUINO_TINYUSB_CONSOLE.println("[TUD] step: tud_rhport_init...");
  const tusb_rhport_init_t tud_init_cfg = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_FULL
  };
  tud_rhport_init(GCM4_RHPORT, &tud_init_cfg);
  ARDUINO_TINYUSB_CONSOLE.println("[TUD] init done");
#endif
}

void tuh_arduino_init(void) {
#if CFG_TUH_ENABLED
  ARDUINO_TINYUSB_CONSOLE.println("[TUH] step: host enable + clocks...");
  // PA27 (Arduino pin 77): USB host enable, must be driven high.
  pinMode(PIN_USB_HOST_ENABLE, OUTPUT);
  digitalWrite(PIN_USB_HOST_ENABLE, HIGH);
  gcm4_tusb_clocks_init();
  s_host_mode = true;
  // NOTE: Do NOT enable USB_0..3_IRQn here — same reason as
  // tud_arduino_init(). tuh_rhport_init() → hcd_init() configures host
  // mode, then hcd_int_enable() enables the IRQs safely.
  _gcm4_mode = ARDUINO_TINYUSB_MODE_HOST;
  ARDUINO_TINYUSB_CONSOLE.println("[TUH] step: tuh_rhport_init...");
  const tusb_rhport_init_t tuh_init_cfg = {
    .role = TUSB_ROLE_HOST,
    .speed = TUSB_SPEED_FULL,
  };
  tuh_rhport_init(GCM4_RHPORT, &tuh_init_cfg);
  ARDUINO_TINYUSB_CONSOLE.println("[TUH] init done");
#endif
}

// Host task pump (plain tuh_task() wrapper; see bsp_zero.cpp note).
void tuh_arduino_poll(void) {
#if CFG_TUH_ENABLED
  tuh_task();
#endif
}

void arduino_tinyusb_probe(void) {
  char line[192];
  snprintf(line, sizeof(line),
    "[GCM4] probe MCLK APBB_USB=%lu AHB_USB=%lu PCHCTRL[%d]=%08lx (GCLK1 GEN=%lu)",
    (unsigned long)((MCLK->APBBMASK.reg & MCLK_APBBMASK_USB) ? 1 : 0),
    (unsigned long)((MCLK->AHBMASK.reg & MCLK_AHBMASK_USB) ? 1 : 0),
    USB_GCLK_ID, (unsigned long)GCLK->PCHCTRL[USB_GCLK_ID].reg,
    (unsigned long)GCLK->GENCTRL[1].reg);
  ARDUINO_TINYUSB_CONSOLE.println(line);
  snprintf(line, sizeof(line),
    "[GCM4] probe USB CTRLA=%08lx CTRLB=%08lx INTFLAG=%08lx INTENSET=%08lx",
    (unsigned long)USB->DEVICE.CTRLA.reg, (unsigned long)USB->DEVICE.CTRLB.reg,
    (unsigned long)USB->DEVICE.INTFLAG.reg, (unsigned long)USB->DEVICE.INTENSET.reg);
  ARDUINO_TINYUSB_CONSOLE.println(line);
  snprintf(line, sizeof(line),
    "[GCM4] probe DADD=%02lx FSM=%02lx EPINTSMRY=%04lx",
    (unsigned long)USB->DEVICE.DADD.reg,
    (unsigned long)USB->DEVICE.FSMSTATUS.reg,
    (unsigned long)USB->DEVICE.EPINTSMRY.reg);
  ARDUINO_TINYUSB_CONSOLE.println(line);
  snprintf(line, sizeof(line),
    "[GCM4] probe EP0 CFG=%02lx STATUS=%02lx INTFLAG=%02lx INTENSET=%02lx",
    (unsigned long)USB->DEVICE.DeviceEndpoint[0].EPCFG.reg,
    (unsigned long)USB->DEVICE.DeviceEndpoint[0].EPSTATUS.reg,
    (unsigned long)USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.reg,
    (unsigned long)USB->DEVICE.DeviceEndpoint[0].EPINTENSET.reg);
  ARDUINO_TINYUSB_CONSOLE.println(line);
}

// Mode tracking (GCM4 is single-port, device XOR host at runtime)
ArduinoTinyUSBMode arduino_tinyusb_get_mode(void) { return _gcm4_mode; }

#endif // ARDUINO_TINYUSB_BOARD_GCM4
