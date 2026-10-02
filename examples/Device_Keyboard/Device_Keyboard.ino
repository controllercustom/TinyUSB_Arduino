// Device_Keyboard.ino — Boot HID keyboard that types "hello" repeatedly.
//
// Works on ALL boards:
//   Due: Serial = programming port (UART via ATmega16U2)
//   Zero/GIGA: Serial1 = UART (native USB owned by TinyUSB)

#include <ArduinoTinyUSB.h>

static uint32_t prev_ms = 0;

// HID keycodes (USB HID Usage Tables)
enum {
  KEY_H = 0x0b,
  KEY_E = 0x08,
  KEY_L = 0x0f,
  KEY_O = 0x12,
  KEY_RETURN = 0x28,
  KEY_NONE = 0x00,
};

static const uint8_t hello_keys[] = { KEY_H, KEY_E, KEY_L, KEY_L, KEY_O, KEY_RETURN };
static uint8_t key_index = 0;
static bool key_sent = false;

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it.
  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[KBD] Arduino-TinyUSB HID Keyboard");

  ARDUINO_TINYUSB_CONSOLE.println("[KBD] Device stack initialized. Waiting for host...");
}

void loop() {
  tud_arduino_task();

  uint32_t now = millis();
  if (now - prev_ms < 100) return;  // 100ms between keys
  prev_ms = now;

  if (!tud_hid_ready()) return;

  if (!key_sent) {
    // Send key press
    uint8_t report[8] = { 0 };
    report[2] = hello_keys[key_index];  // keycodes[0]
    tud_hid_keyboard_report(0, 0, report);
    key_sent = true;
    ARDUINO_TINYUSB_CONSOLE.print("[KBD] Press: 0x");
    ARDUINO_TINYUSB_CONSOLE.println(hello_keys[key_index], HEX);
  } else {
    // Send key release
    uint8_t report[8] = { 0 };
    tud_hid_keyboard_report(0, 0, report);
    key_sent = false;
    key_index = (key_index + 1) % sizeof(hello_keys);
  }
}
