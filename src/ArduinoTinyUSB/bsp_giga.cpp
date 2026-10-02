// bsp_giga.cpp — TinyUSB BSP glue for Arduino GIGA R1 WiFi.
//
// Giga has TWO independent USB controllers:
//   rhport 0 (OTG_FS, USB Type C) — device mode, owned by Mbed core for CDC serial
//   rhport 1 (OTG_HS, USB Type A) — host mode, owned by TinyUSB
//
// Both can run simultaneously. Device examples run on Type C, host examples
// on Type A. Giga can run both sides in a single sketch.
//
// What Mbed already owns (do NOT touch): clocks/RCC base, USB-C CDC console
// (OTG_FS), SysTick, NVIC core config.

#include "board_auto.h"
#if defined(ARDUINO_TINYUSB_BOARD_GIGA)

#include "Arduino.h"
#include "pinmap.h"
#include "stm32h7xx_hal_rcc.h"
#include "stm32h7xx_hal_rcc_ex.h"
#include "stm32h7xx_hal_gpio.h"
#include "usb_phy_api.h"
#include "../ArduinoTinyUSB.h"
#include "bsp_common.h"

// VBUS enable pin for USB Type A host port
#ifndef ARDUINO_TINYUSB_VBUS_PIN
  #define ARDUINO_TINYUSB_VBUS_PIN PA_15
#endif

// OTG_HS host interrupt -> TinyUSB HCD (rhport 1).
extern "C" void OTG_HS_IRQHandler(void)
{
#if CFG_TUH_ENABLED
  hcd_int_handler(1, true);
#endif
}

// OTG_FS device interrupt -> TinyUSB DCD (rhport 0).
// NOTE: Mbed installs its own OTG_FS handler at runtime via NVIC_SetVector
// during its boot-time CDC init, so this link-time override alone is NOT
// enough — tud_arduino_init() must reclaim the RAM vector (see below).
extern "C" void OTG_FS_IRQHandler(void)
{
#if CFG_TUD_ENABLED
  dcd_int_handler(0);
#endif
}

// ─── Device Init (rhport 0, USB Type C) ───
void tud_arduino_init(void)
{
#if CFG_TUD_ENABLED
  ARDUINO_TINYUSB_CONSOLE.println("[TUD] step: release Mbed USB...");

  // Mbed boots its own USB device (CDC 2341:0266) on OTG_FS before setup().
  // Soft-disconnect it so it stops driving D+/D- (clocks/PHY stay up —
  // TinyUSB needs the Mbed-started HSI48 48 MHz reference).
  USBPhy *phy = get_usb_phy();
  if (phy) phy->disconnect();
  delay(50);

  ARDUINO_TINYUSB_CONSOLE.println("[TUD] step: reclaim OTG_FS...");
  // Reclaim the IRQ vector Mbed overwrote at runtime, then reset the
  // peripheral to clear Mbed's device state (dwc2 dcd_init resets again).
  NVIC_SetVector(OTG_FS_IRQn, (uint32_t)OTG_FS_IRQHandler);
  __HAL_RCC_USB_OTG_FS_FORCE_RESET();
  __HAL_RCC_USB_OTG_FS_RELEASE_RESET();

  // Clocks + PA11/PA12 (DM/DP) to AF10 OTG_FS. Do NOT rely on Mbed's
  // pinmux leftovers — phy disconnect/deinit paths may park pins.
#if defined(GPIO_AF10_OTG1_FS)
#define GIGA_OTG_FS_AF GPIO_AF10_OTG1_FS
#elif defined(GPIO_AF10_OTG2_FS)
#define GIGA_OTG_FS_AF GPIO_AF10_OTG2_FS
#else
#define GIGA_OTG_FS_AF 0x0A
#endif
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
#pragma GCC diagnostic pop
  pin_function(PA_11, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_NOPULL, GIGA_OTG_FS_AF));
  pin_function(PA_12, STM_PIN_DATA(STM_MODE_AF_PP, GPIO_NOPULL, GIGA_OTG_FS_AF));

  // Configure OTG_FS interrupts
  NVIC_SetPriority(OTG_FS_IRQn, ARDUINO_TINYUSB_NVIC_PRIO);
  NVIC_ClearPendingIRQ(OTG_FS_IRQn);
  NVIC_EnableIRQ(OTG_FS_IRQn);

  ARDUINO_TINYUSB_CONSOLE.println("[TUD] step: tud_rhport_init...");
  const tusb_rhport_init_t tud_init_cfg = {
    .role = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_FULL
  };
  tud_rhport_init(0, &tud_init_cfg);
  ARDUINO_TINYUSB_CONSOLE.println("[TUD] init done");
#endif
}

// ─── Host Init (rhport 1, USB Type A) ───
// Internally handles VBUS power + settle delay.
void tuh_arduino_init(void)
{
#if CFG_TUH_ENABLED
  ARDUINO_TINYUSB_CONSOLE.println("[TUH] step: VBUS enable...");

  // Power USB-A VBUS (PA_15 HIGH)
  pinMode(ARDUINO_TINYUSB_VBUS_PIN, OUTPUT);
  digitalWrite(ARDUINO_TINYUSB_VBUS_PIN, HIGH);
  delay(1000);  // VBUS settle time

  ARDUINO_TINYUSB_CONSOLE.println("[TUH] step: pins...");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
  __HAL_RCC_GPIOB_CLK_ENABLE();
#pragma GCC diagnostic pop
  pin_function(PB_14, STM_PIN_DEFINE_SPEED(STM_MODE_AF_PP, GPIO_NOPULL, GPIO_AF12_OTG2_FS, GPIO_SPEED_FREQ_VERY_HIGH)); // DM
  pin_function(PB_15, STM_PIN_DEFINE_SPEED(STM_MODE_AF_PP, GPIO_NOPULL, GPIO_AF12_OTG2_FS, GPIO_SPEED_FREQ_VERY_HIGH)); // DP

  ARDUINO_TINYUSB_CONSOLE.println("[TUH] step: rcc...");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
  __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();
#pragma GCC diagnostic pop
  // NOTE: do NOT enable the ULPI clock: Giga has no ULPI PHY chip, and a
  // clocked-but-floating ULPI wrapper holds the DWC2 core in reset.
  // NOTE: no HSI48/CRS setup needed — Mbed already runs HSI48 for CDC.

  ARDUINO_TINYUSB_CONSOLE.print("[TUH] AHB1ENR="); ARDUINO_TINYUSB_CONSOLE.println(RCC->AHB1ENR, HEX);

  // Configure OTG_HS interrupts
  NVIC_SetPriority(OTG_HS_IRQn, ARDUINO_TINYUSB_NVIC_PRIO);
  NVIC_EnableIRQ(OTG_HS_IRQn);

  ARDUINO_TINYUSB_CONSOLE.println("[TUH] step: tuh_init on rhport 1 (OTG_HS, Type A)...");
  const tusb_rhport_init_t tuh_init_cfg = {
    .role = TUSB_ROLE_HOST,
    .speed = TUSB_SPEED_FULL
  };
  tuh_rhport_init(1, &tuh_init_cfg);
  ARDUINO_TINYUSB_CONSOLE.println("[TUH] init done");
#endif
}

// Host task pump (plain tuh_task() wrapper; see bsp_due.cpp note).
void tuh_arduino_poll(void) {
#if CFG_TUH_ENABLED
  tuh_task();
#endif
}

// ─── Probe ───
void arduino_tinyusb_probe(void)
{
  uint32_t gsnpsid = ((volatile uint32_t *)0x40040040UL)[0];
  uint32_t ghwcfg2 = ((volatile uint32_t *)0x40040048UL)[0];
  uint32_t grstctl = ((volatile uint32_t *)0x40040010UL)[0];
  uint32_t gusbcfg = ((volatile uint32_t *)0x4004000CUL)[0];
  uint32_t gccfg   = ((volatile uint32_t *)0x40040038UL)[0];
  uint32_t rcc_cr  = RCC->CR;
  uint32_t usbsel  = (RCC->D2CCIP2R >> 20) & 0x3UL;
  uint32_t ahb1enr = RCC->AHB1ENR;
  char line[160];
  snprintf(line, sizeof(line),
    "[GIGA] probe GSNPSID=%08lx GHWCFG2=%08lx GRSTCTL=%08lx(AHBIDL=%lu CSRST=%lu)",
    (unsigned long)gsnpsid, (unsigned long)ghwcfg2, (unsigned long)grstctl,
    (unsigned long)((grstctl >> 31) & 1), (unsigned long)(grstctl & 1));
  ARDUINO_TINYUSB_CONSOLE.println(line);
  snprintf(line, sizeof(line),
    "[GIGA] probe GUSBCFG_PHYSEL=%lu GCCFG_PWRDWN=%lu HSI48ON=%lu HSIRDY=%lu USBSRC=%lu OTGHSEN=%lu",
    (unsigned long)((gusbcfg >> 6) & 1), (unsigned long)((gccfg >> 16) & 1),
    (unsigned long)((rcc_cr >> 12) & 1), (unsigned long)((rcc_cr >> 13) & 1),
    (unsigned long)usbsel, (unsigned long)((ahb1enr >> 25) & 1));
  ARDUINO_TINYUSB_CONSOLE.println(line);
}

// Mode tracking (Giga: device + host can run simultaneously)
ArduinoTinyUSBMode _giga_mode = ARDUINO_TINYUSB_MODE_UNINITIALIZED;
ArduinoTinyUSBMode arduino_tinyusb_get_mode(void) { return _giga_mode; }

#endif // ARDUINO_TINYUSB_BOARD_GIGA
