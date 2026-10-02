// Device_HID.ino — Boot HID mouse that moves cursor in a figure-8 pattern.
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC

#include <ArduinoTinyUSB.h>

static uint32_t prev_ms = 0;
static uint32_t t = 0;

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it, avoiding a mid-enumeration
  // device swap that wedges the host port.
  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[HID] Arduino-TinyUSB HID Mouse");

  ARDUINO_TINYUSB_CONSOLE.println("[HID] Device stack initialized.");
}

void loop() {
  tud_arduino_task();

  uint32_t now = millis();
  if (now - prev_ms < 8) return;
  prev_ms = now;

  if (!tud_hid_ready()) return;

  t += 2;
  int8_t dx = (int8_t)((t * 3) & 0xFF);
  int8_t dy = (int8_t)((t * 7) & 0xFF);

  tud_hid_mouse_report(0x00, 0, dx, dy, 0, 0);
}
