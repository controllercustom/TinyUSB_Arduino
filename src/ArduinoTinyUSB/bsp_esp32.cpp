// bsp_esp32.cpp — TinyUSB BSP glue for Espressif ESP32 family (phase 1: S3 device-only).
//
// ESP32-S3 native USB (GPIO19 D- / GPIO20 D+, dedicated pins, no mux work)
// is a Synopsys DWC2 OTG FS controller; the vendored dwc2_esp32.h port glue
// (clocks, ISR via esp_intr_alloc, single-port table) plus dcd_dwc2.c own
// the low-level bring-up. This file only tracks mode, forwards init/poll,
// and exposes probe state. Single controller: device XOR host at runtime
// (same model as Pico/Zero); host (VBUS power + OTG adapter) is phase 3.

#include "board_auto.h"
#if defined(ARDUINO_TINYUSB_BOARD_ESP32)

#include "Arduino.h"
#include "../ArduinoTinyUSB.h"
#include "bsp_common.h"

// ESP32-S2/S3 DWC2 needs a module reset after ROM/USB-download activity:
// the ROM leaves DWC2/PHY in persist or USJ state and nothing enumerates
// until reset (mirrors the core's esp32-hal-tinyusb.c init sequence).
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/periph_defs.h"
#if CONFIG_IDF_TARGET_ESP32S3
#include "soc/usb_serial_jtag_reg.h"
#endif
#include "hal/clk_gate_ll.h"
#include "hal/usb_phy_types.h"
#include "esp_private/usb_phy.h"

#define ESP32_RHPORT 0

// Mode tracking (single-port, device XOR host at runtime)
static ArduinoTinyUSBMode _esp32_mode = ARDUINO_TINYUSB_MODE_UNINITIALIZED;

static void esp32_usb_module_reset(void) {
  REG_CLR_BIT(RTC_CNTL_USB_CONF_REG, RTC_CNTL_IO_MUX_RESET_DISABLE);
  REG_CLR_BIT(RTC_CNTL_USB_CONF_REG, RTC_CNTL_USB_RESET_DISABLE);
  periph_ll_reset(PERIPH_USB_MODULE);
  periph_ll_enable_clk_clear_rst(PERIPH_USB_MODULE);
#if CONFIG_IDF_TARGET_ESP32S3
  // S3: release D+/D- (GPIO19/20) from USB-Serial-JTAG to the OTG
  // controller. The pins are shared: ROM/JTAG owns them at boot (hence
  // 303a:1001 on the bus); without this the OTG peripheral never sees
  // the host. (S2 pad routing is owned by the usb_phy driver instead.)
  REG_CLR_BIT(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
#endif
}

// Power and configure the internal FS PHY (mirrors what IDF's
// tinyusb_driver_install does; upstream TinyUSB leaves this to IDF).
// Mode-parameterized: device XOR host at runtime (same model as Pico).
static void esp32_phy_init(usb_otg_mode_t mode) {
  static usb_phy_handle_t s_phy_dev = NULL;
  static usb_phy_handle_t s_phy_host = NULL;
  usb_phy_handle_t *s_phy = (mode == USB_OTG_MODE_HOST) ? &s_phy_host : &s_phy_dev;
  if (*s_phy == NULL) {
    const usb_phy_config_t phy_conf = {
      .controller = USB_PHY_CTRL_OTG,
      .target = USB_PHY_TARGET_INT,
      .otg_mode = mode,
      .otg_speed = USB_PHY_SPEED_FULL,
      .ext_io_conf = NULL,
      .otg_io_conf = NULL,
    };
    if (usb_new_phy(&phy_conf, s_phy) != ESP_OK) {
      *s_phy = NULL;
    }
  }
}

void tud_arduino_init(void) {
#if CFG_TUD_ENABLED
  _esp32_mode = ARDUINO_TINYUSB_MODE_DEVICE;
  esp32_usb_module_reset();
  esp32_phy_init(USB_OTG_MODE_DEVICE);
  const tusb_rhport_init_t tud_init_cfg = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_FULL
  };
  tud_rhport_init(ESP32_RHPORT, &tud_init_cfg);
#endif
}

void tuh_arduino_init(void) {
#if CFG_TUH_ENABLED
  _esp32_mode = ARDUINO_TINYUSB_MODE_HOST;
  esp32_usb_module_reset();
  esp32_phy_init(USB_OTG_MODE_HOST);
  const tusb_rhport_init_t tuh_init_cfg = {
    .role = TUSB_ROLE_HOST,
    .speed = TUSB_SPEED_FULL,
  };
  tuh_rhport_init(ESP32_RHPORT, &tuh_init_cfg);
#endif
}

// Host task pump: non-blocking poll on ESP32 (bare tuh_task() blocks on the
// FreeRTOS queue like tud_task() did — same freeze, same fix).
void tuh_arduino_poll(void) {
#if CFG_TUH_ENABLED
  tuh_task_ext(0, false);
#endif
}

void arduino_tinyusb_probe(void) {
  // DWC2 core registers (same offsets as Giga probe).
  uint32_t gsnpsid = ((volatile uint32_t *)0x60080040UL)[0];
  uint32_t ghwcfg2 = ((volatile uint32_t *)0x60080048UL)[0];
  uint32_t gusbcfg = ((volatile uint32_t *)0x6008000CUL)[0];
  char line[160];
  snprintf(line, sizeof(line),
    "[ESP32] probe GSNPSID=%08lx GHWCFG2=%08lx GUSBCFG=%08lx",
    (unsigned long)gsnpsid, (unsigned long)ghwcfg2,
    (unsigned long)gusbcfg);
  ARDUINO_TINYUSB_CONSOLE.println(line);
}

// Mode query (state set by tud/tuh_arduino_init above)
ArduinoTinyUSBMode arduino_tinyusb_get_mode(void) { return _esp32_mode; }

#endif // ARDUINO_TINYUSB_BOARD_ESP32
