// Host_HID.ino — Reads HID input reports from mice and keyboards.
//
// Plug a USB HID device (mouse, keyboard, or composite) into the
// host port.  The sketch enumerates it and prints every input report
// it receives, with human-readable decoding for boot-protocol mice
// and keyboards.
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC

#include "ArduinoTinyUSB.h"

// ── Helpers ──────────────────────────────────────────────────────────

static const char *mod_string(uint8_t mod) {
  static char buf[8];
  buf[0] = (mod & 0x11) ? 'C' : '-';
  buf[1] = (mod & 0x22) ? 'S' : '-';
  buf[2] = (mod & 0x44) ? 'A' : '-';
  buf[3] = (mod & 0x88) ? 'G' : '-';
  buf[4] = '\0';
  return buf;
}

static void print_keycode(uint8_t kc) {
  if (kc == 0) return;
  if (kc >= 0x04 && kc <= 0x1D) {                     // a-z
    ARDUINO_TINYUSB_CONSOLE.write('a' + kc - 0x04);
  } else if (kc >= 0x1E && kc <= 0x27) {               // 1-9, 0
    ARDUINO_TINYUSB_CONSOLE.write('1' + kc - 0x1E);
  } else {
    ARDUINO_TINYUSB_CONSOLE.print(F("0x"));
    ARDUINO_TINYUSB_CONSOLE.print(kc, HEX);
  }
}

// ── TinyUSB Host callbacks ───────────────────────────────────────────

extern "C" void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx,
                                  const uint8_t *report_desc,
                                  uint16_t desc_len) {
  uint8_t proto = tuh_hid_interface_protocol(dev_addr, idx);

  ARDUINO_TINYUSB_CONSOLE.print(F("\n── HID Mounted ──\n"));
  ARDUINO_TINYUSB_CONSOLE.print(F("  addr=")); ARDUINO_TINYUSB_CONSOLE.print(dev_addr);
  ARDUINO_TINYUSB_CONSOLE.print(F("  itf=")); ARDUINO_TINYUSB_CONSOLE.print(idx);
  ARDUINO_TINYUSB_CONSOLE.print(F("  proto="));
  if (proto == HID_ITF_PROTOCOL_KEYBOARD) ARDUINO_TINYUSB_CONSOLE.println(F("Keyboard"));
  else if (proto == HID_ITF_PROTOCOL_MOUSE) ARDUINO_TINYUSB_CONSOLE.println(F("Mouse"));
  else ARDUINO_TINYUSB_CONSOLE.println(F("Other"));

  // Start receiving reports from this interface
  tuh_hid_receive_report(dev_addr, idx);
}

extern "C" void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx) {
  ARDUINO_TINYUSB_CONSOLE.print(F("\n── HID Removed ── addr="));
  ARDUINO_TINYUSB_CONSOLE.print(dev_addr);
  ARDUINO_TINYUSB_CONSOLE.print(F("  itf="));
  ARDUINO_TINYUSB_CONSOLE.println(idx);
}

extern "C" void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx,
                                            const uint8_t *report, uint16_t len) {
  uint8_t proto = tuh_hid_interface_protocol(dev_addr, idx);

  if (proto == HID_ITF_PROTOCOL_MOUSE && len >= 4) {
    const hid_mouse_report_t *m = (const hid_mouse_report_t *)report;
    ARDUINO_TINYUSB_CONSOLE.print(F("Mouse b="));
    ARDUINO_TINYUSB_CONSOLE.print(m->buttons, HEX);
    ARDUINO_TINYUSB_CONSOLE.print(F(" x="));
    ARDUINO_TINYUSB_CONSOLE.print(m->x);
    ARDUINO_TINYUSB_CONSOLE.print(F(" y="));
    ARDUINO_TINYUSB_CONSOLE.print(m->y);
    ARDUINO_TINYUSB_CONSOLE.print(F(" wh="));
    ARDUINO_TINYUSB_CONSOLE.println(m->wheel);

  } else if (proto == HID_ITF_PROTOCOL_KEYBOARD && len >= 8) {
    const hid_keyboard_report_t *k = (const hid_keyboard_report_t *)report;
    ARDUINO_TINYUSB_CONSOLE.print(F("Key mod="));
    ARDUINO_TINYUSB_CONSOLE.print(mod_string(k->modifier));
    ARDUINO_TINYUSB_CONSOLE.print(F(" ["));
    for (uint8_t i = 0; i < 6; i++) {
      if (k->keycode[i] != 0) {
        if (i > 0) ARDUINO_TINYUSB_CONSOLE.write(' ');
        print_keycode(k->keycode[i]);
      }
    }
    ARDUINO_TINYUSB_CONSOLE.println(F("]"));

  } else {
    // Unknown / vendor-specific: hex dump
    ARDUINO_TINYUSB_CONSOLE.print(F("Report itf="));
    ARDUINO_TINYUSB_CONSOLE.print(idx);
    ARDUINO_TINYUSB_CONSOLE.print(F(" len="));
    ARDUINO_TINYUSB_CONSOLE.print(len);
    ARDUINO_TINYUSB_CONSOLE.print(F(" data="));
    for (uint16_t i = 0; i < len && i < 32; i++) {
      if (report[i] < 0x10) ARDUINO_TINYUSB_CONSOLE.write('0');
      ARDUINO_TINYUSB_CONSOLE.print(report[i], HEX);
    }
    ARDUINO_TINYUSB_CONSOLE.println();
  }

  // Re-arm for the next report
  tuh_hid_receive_report(dev_addr, idx);
}

// ── Arduino setup / loop ────────────────────────────────────────────

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(115200);
  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { /* wait for Serial */ }

  ARDUINO_TINYUSB_CONSOLE.println(F("\n=== TinyUSB Host HID ==="));

  // Initialise TinyUSB host stack
  tuh_arduino_init();

  ARDUINO_TINYUSB_CONSOLE.println(F("Waiting for HID devices...\n"));
}

#if defined(ARDUINO_TINYUSB_BOARD_DUE)
static bool _dumped = false;
#endif

void loop() {
  // Run the TinyUSB host task — drives enumeration & callbacks
  tuh_arduino_poll();

#if defined(ARDUINO_TINYUSB_BOARD_DUE)
  // Dump ring log 8s after boot (after enumeration attempt completes/fails)
  if (!_dumped && millis() > 8000) {
    _dumped = true;
    uint32_t ring[64];
    hcd_sam3x_ring(ring);
    ARDUINO_TINYUSB_CONSOLE.println(F("\n--- HCD Ring Log ---"));
    for (int i = 0; i < 64; i++) {
      uint8_t code = ring[i] >> 24;
      uint8_t pipe = (ring[i] >> 16) & 0xFF;
      uint8_t a = (ring[i] >> 8) & 0xFF;
      uint8_t b = ring[i] & 0xFF;
      if (code == 0) continue;
      char line[48];
      snprintf(line, sizeof(line), "  [%2d] code=%d pipe=%d a=%d b=%d",
               i, code, pipe, a, b);
      ARDUINO_TINYUSB_CONSOLE.println(line);
    }
    ARDUINO_TINYUSB_CONSOLE.println(F("--- End Ring Log ---\n"));
    arduino_tinyusb_probe();
  }
#endif
}
