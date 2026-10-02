// SPDX-License-Identifier: MIT
// Device_Vendor.ino — USB Vendor class echo (device mode).
//
// VID:PID come from tud_arduino_set_vidpid(); the usb_descriptors.cpp tab
// serves the CDC + Vendor layout.
//
// Works on ALL boards (ArduinoTinyUSB multi-board library).
// Debug: ARDUINO_TINYUSB_CONSOLE @ ARDUINO_TINYUSB_BAUD.
//
// NOTE: the source sketch's tuh_cdc_rx_cb() (a host-stack callback) is
// dropped — it can never fire in device mode.

#include "ArduinoTinyUSB.h"

#define USB_VID 0x2341
#define USB_PID 0x0058

void setup()
{
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it, avoiding a mid-enumeration
  // device swap that wedges the host port.
  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[Vendor] ArduinoTinyUSB Vendor Echo");
  ARDUINO_TINYUSB_CONSOLE.println("[Vendor] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  /* Echo vendor data from host (standard TinyUSB vendor API, all boards) */
  while (tud_vendor_available()) {
    uint8_t buf[64];
    uint32_t n = tud_vendor_read(buf, sizeof(buf));
    if (n) {
      tud_vendor_write(buf, n);
      tud_vendor_write_flush();
    }
  }
}
