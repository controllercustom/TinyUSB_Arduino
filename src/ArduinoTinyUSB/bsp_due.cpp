// bsp_due.cpp — TinyUSB BSP glue for Arduino Due (ATSAM3X8E UOTGHS).
//
// Hardware init recipe lifted from libsam (system/libsam/source/):
//   device: uotghs_device.c UDD_Init() — pmc_enable_periph_clk(ID_UOTGHS),
//     pmc_enable_upll_clock(), pmc_switch_udpck_to_upllck(0), pmc_enable_udpck,
//     otg_disable_id_pin() + otg_force_device_mode(), otg_enable_pad/enable,
//     otg_unfreeze_clock.
//   host: uotghs_host.c UHD_Init() — same clocks, otg_force_host_mode().
// IRQ: libsam uotghs.c UOTGHS_Handler() dispatches via gpf_isr, installed with
// UDD_SetStack()/UHD_SetStack(). We hook there instead of overriding the
// handler, so there is no link conflict with the SAM core (whose
// cortex_handlers.c UOTGHS_Handler is weak-aliased to __halt).

#include "board_auto.h"
#if defined(ARDUINO_TINYUSB_BOARD_DUE)

#include "Arduino.h"
#include "../ArduinoTinyUSB.h"
#include "bsp_common.h"

extern "C" {
#include "chip.h"
}
// TinyUSB port drivers (src/portable/microchip/sam3x/).
extern "C" void dcd_sam3x_isr(void);
extern "C" void hcd_sam3x_isr(void);

#define DUE_RHPORT 0

static void due_tusb_clocks_init(void) {
  pmc_enable_periph_clk(ID_UOTGHS);
  pmc_enable_upll_clock();
  pmc_switch_udpck_to_upllck(0); // div = 0+1 -> 48 MHz from 480 MHz UPLL
  pmc_enable_udpck();
}

void tud_arduino_init(void) {
#if CFG_TUD_ENABLED
  ARDUINO_TINYUSB_CONSOLE.println("[TUD] step: clocks...");
  due_tusb_clocks_init();
  ARDUINO_TINYUSB_CONSOLE.println("[TUD] step: device mode...");
  otg_disable_id_pin();
  otg_force_device_mode();
  otg_disable_pad();
  otg_enable_pad();
  otg_enable();
  otg_unfreeze_clock();
  // Force full-speed (Klipper sam3_usb.c does the same on this exact chip).
  // Default SPDCONF_NORMAL attempts a high-speed reset when the host is
  // HS-capable; the Due Native-port wiring/PHY is proven at FS (USBCore CDC).
  UOTGHS->UOTGHS_DEVCTRL |= UOTGHS_DEVCTRL_SPDCONF_FORCED_FS;
  UDD_SetStack(dcd_sam3x_isr);
  NVIC_SetPriority((IRQn_Type)ID_UOTGHS, ARDUINO_TINYUSB_NVIC_PRIO);
  NVIC_EnableIRQ((IRQn_Type)ID_UOTGHS);
  ARDUINO_TINYUSB_CONSOLE.println("[TUD] step: tud_init...");
  const tusb_rhport_init_t tud_init_cfg = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_FULL
  };
  tud_rhport_init(DUE_RHPORT, &tud_init_cfg);
  ARDUINO_TINYUSB_CONSOLE.println("[TUD] init done");
#endif
}

void tuh_arduino_init(void) {
#if CFG_TUH_ENABLED
  ARDUINO_TINYUSB_CONSOLE.println("[TUH] step: clocks...");
  due_tusb_clocks_init();
  ARDUINO_TINYUSB_CONSOLE.println("[TUH] step: host mode...");
  otg_disable_id_pin();
  otg_force_host_mode();
  otg_disable_pad();
  otg_enable_pad();
  otg_enable();
  otg_unfreeze_clock();
  // Due-specific VBUS power: VBOF is active-HIGH on the Due (libsam UHD_Init
  // carries the same note). Without this the downstream port stays unpowered
  // and no device ever connects.
  uhd_set_vbof_active_high();
  uhd_enable_vbus();
  otg_ack_vbus_transition();
  Set_bits(UOTGHS->UOTGHS_CTRL,
      UOTGHS_CTRL_VBUSHWC | UOTGHS_CTRL_VBUSTE | UOTGHS_CTRL_VBERRE);
  UHD_SetStack(hcd_sam3x_isr);
  NVIC_SetPriority((IRQn_Type)ID_UOTGHS, ARDUINO_TINYUSB_NVIC_PRIO);
  NVIC_EnableIRQ((IRQn_Type)ID_UOTGHS);
  ARDUINO_TINYUSB_CONSOLE.println("[TUH] step: tuh_init...");
  const tusb_rhport_init_t tuh_init_cfg = {
    .role = TUSB_ROLE_HOST,
    .speed = TUSB_SPEED_FULL,
  };
  tuh_rhport_init(DUE_RHPORT, &tuh_init_cfg);
  ARDUINO_TINYUSB_CONSOLE.println("[TUH] init done");
#endif
}

// Host task pump. On Due this is a plain tuh_task() wrapper (kept so all
// Host_* example loops share one call).
void tuh_arduino_poll(void) {
#if CFG_TUH_ENABLED
  tuh_task();
#endif
}

void arduino_tinyusb_probe(void) {
#if CFG_TUD_ENABLED
  uint32_t ctrl = UOTGHS->UOTGHS_CTRL;
  uint32_t devctrl = UOTGHS->UOTGHS_DEVCTRL;
  uint32_t devisr = UOTGHS->UOTGHS_DEVISR;
  uint32_t devimr = UOTGHS->UOTGHS_DEVIMR;
  uint32_t ep0isr = UOTGHS->UOTGHS_DEVEPTISR[0];
  uint32_t sr = PMC->PMC_SR;
  char line[160];
  snprintf(line, sizeof(line),
    "[DUE] probe CTRL=%08lx DEVCTRL=%08lx(DETACH=%lu ADDEN=%lu UADD=%lu)",
    (unsigned long)ctrl, (unsigned long)devctrl,
    (unsigned long)((devctrl >> 8) & 1), (unsigned long)((devctrl >> 7) & 1),
    (unsigned long)(devctrl & 0x7F));
  ARDUINO_TINYUSB_CONSOLE.println(line);
  snprintf(line, sizeof(line),
    "[DUE] probe DEVISR=%08lx DEVIMR=%08lx EP0ISR=%08lx LOCKU=%lu UOTGCLK=%lu",
    (unsigned long)devisr, (unsigned long)devimr, (unsigned long)ep0isr,
    (unsigned long)((sr & PMC_SR_LOCKU) ? 1 : 0),
    (unsigned long)((PMC->PMC_SCSR & PMC_SCER_UOTGCLK) ? 1 : 0));
  ARDUINO_TINYUSB_CONSOLE.println(line);
  snprintf(line, sizeof(line),
    "[DUE] ep1 ISR=%08lx IMR=%08lx CFG=%08lx | ep2 ISR=%08lx IMR=%08lx CFG=%08lx",
    (unsigned long)UOTGHS->UOTGHS_DEVEPTISR[1], (unsigned long)UOTGHS->UOTGHS_DEVEPTIMR[1],
    (unsigned long)UOTGHS->UOTGHS_DEVEPTCFG[1],
    (unsigned long)UOTGHS->UOTGHS_DEVEPTISR[2], (unsigned long)UOTGHS->UOTGHS_DEVEPTIMR[2],
    (unsigned long)UOTGHS->UOTGHS_DEVEPTCFG[2]);
  ARDUINO_TINYUSB_CONSOLE.println(line);
  int s7_kick, s7_rxouti, s7_complete, s7_no_active, s7_no_rxoute, s7_cfgok_fail;
  dcd_sam3x_stats7(&s7_kick, &s7_rxouti, &s7_complete, &s7_no_active, &s7_no_rxoute, &s7_cfgok_fail);
  snprintf(line, sizeof(line),
    "[DUE] EP1 kick=%d rxouti=%d complete=%d no_active=%d no_rxoute=%d cfgok_fail=%d",
    s7_kick, s7_rxouti, s7_complete, s7_no_active, s7_no_rxoute, s7_cfgok_fail);
  ARDUINO_TINYUSB_CONSOLE.println(line);
#else
  // Device stack disabled (host-only config): no DCD stats to dump.
  ARDUINO_TINYUSB_CONSOLE.println("[DUE] probe: device stack disabled");
#endif
}

// Mode tracking (Due is single-port, device XOR host at runtime)
ArduinoTinyUSBMode _due_mode = ARDUINO_TINYUSB_MODE_UNINITIALIZED;
ArduinoTinyUSBMode arduino_tinyusb_get_mode(void) { return _due_mode; }

#endif // ARDUINO_TINYUSB_BOARD_DUE
