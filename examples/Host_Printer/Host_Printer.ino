// Host_Printer.ino — Sends data to a USB printer.
//
// Plug a USB printer into the host port.  The sketch enumerates it,
// reads the device ID and port status, then sends a short text job.
//
// Requires the printer host driver (class/printer/printer_host.c).

#include "ArduinoTinyUSB.h"

// ── Printer State ─────────────────────────────────────────────────

static bool printerMounted = false;
static uint8_t printerDevAddr = 0;

// ── Helpers ───────────────────────────────────────────────────────

static void print_port_status(uint8_t dev_addr) {
  uint8_t status = 0;
  if (tuh_printer_get_status(dev_addr, &status)) {
    tusb_printer_port_status_t *ps = (tusb_printer_port_status_t *)&status;
    ARDUINO_TINYUSB_CONSOLE.print(F("  Status: 0x"));
    ARDUINO_TINYUSB_CONSOLE.print(status, HEX);
    ARDUINO_TINYUSB_CONSOLE.print(F("  ["));
    if (ps->status_bm.selected)    ARDUINO_TINYUSB_CONSOLE.print(F("Selected "));
    if (ps->status_bm.not_error)   ARDUINO_TINYUSB_CONSOLE.print(F("OK "));
    if (ps->status_bm.paper_empty) ARDUINO_TINYUSB_CONSOLE.print(F("PaperEmpty "));
    ARDUINO_TINYUSB_CONSOLE.println(F("]"));
  } else {
    ARDUINO_TINYUSB_CONSOLE.println(F("  Status: unavailable"));
  }
}

static void print_device_id(uint8_t dev_addr) {
  uint8_t id_buf[128];
  uint16_t id_len = tuh_printer_get_device_id(dev_addr, id_buf, sizeof(id_buf));
  if (id_len > 0) {
    // Device ID is a length-prefixed string (2-byte big-endian length, then UTF-8 text)
    if (id_len > 2) {
      ARDUINO_TINYUSB_CONSOLE.print(F("  Device ID: "));
      for (uint16_t i = 2; i < id_len; i++) {
        if (id_buf[i] >= 0x20 && id_buf[i] < 0x7F) {
          ARDUINO_TINYUSB_CONSOLE.write(id_buf[i]);
        }
      }
      ARDUINO_TINYUSB_CONSOLE.println();
    }
  } else {
    ARDUINO_TINYUSB_CONSOLE.println(F("  Device ID: unavailable"));
  }
}

static void send_test_page(uint8_t dev_addr) {
  static const char test_page[] =
    "\x1B@ ..."          // @ = Initialize printer (ESC @)
    "Hello from TinyUSB Host!\r\n"
    "Printer test page.\r\n"
    "\x1Bd\x03"         // ESC d 3 = Print and feed 3 lines
    "\x1B@";             // Initialize again

  ARDUINO_TINYUSB_CONSOLE.println(F("  Sending test page..."));

  uint32_t written = tuh_printer_write(dev_addr, test_page, sizeof(test_page) - 1, NULL, 0);
  ARDUINO_TINYUSB_CONSOLE.print(F("  Wrote "));
  ARDUINO_TINYUSB_CONSOLE.print(written);
  ARDUINO_TINYUSB_CONSOLE.println(F(" bytes"));
}

// ── TinyUSB Host Printer Callbacks ────────────────────────────────

extern "C" {

// Called when a printer is mounted (ready to use)
void tuh_printer_mount_cb(uint8_t dev_addr) {
  ARDUINO_TINYUSB_CONSOLE.print(F("\n── Printer Mounted ── addr="));
  ARDUINO_TINYUSB_CONSOLE.println(dev_addr);

  printerMounted = true;
  printerDevAddr = dev_addr;

  print_device_id(dev_addr);
  print_port_status(dev_addr);

  // Send a test page
  send_test_page(dev_addr);

  ARDUINO_TINYUSB_CONSOLE.println(F("Printer ready.\n"));
}

// Called when a printer is unmounted
void tuh_printer_umount_cb(uint8_t dev_addr) {
  ARDUINO_TINYUSB_CONSOLE.print(F("\n── Printer Removed ── addr="));
  ARDUINO_TINYUSB_CONSOLE.println(dev_addr);

  if (printerMounted && printerDevAddr == dev_addr) {
    printerMounted = false;
  }

  ARDUINO_TINYUSB_CONSOLE.println(F("Waiting for printer...\n"));
}

// Called when data is received from the printer (bidirectional printers only)
void tuh_printer_rx_cb(uint8_t dev_addr, const uint8_t *data, uint16_t len) {
  ARDUINO_TINYUSB_CONSOLE.print(F("[Printer addr="));
  ARDUINO_TINYUSB_CONSOLE.print(dev_addr);
  ARDUINO_TINYUSB_CONSOLE.print(F("] len="));
  ARDUINO_TINYUSB_CONSOLE.print(len);
  ARDUINO_TINYUSB_CONSOLE.print(F(" data="));

  for (uint16_t i = 0; i < len && i < 64; i++) {
    if (data[i] < 0x10) ARDUINO_TINYUSB_CONSOLE.write('0');
    ARDUINO_TINYUSB_CONSOLE.print(data[i], HEX);
  }
  ARDUINO_TINYUSB_CONSOLE.println();
}

} // extern "C"

// ── Arduino setup / loop ──────────────────────────────────────────

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(115200);
  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { /* wait for Serial */ }

  ARDUINO_TINYUSB_CONSOLE.println(F("\n=== TinyUSB Host Printer ==="));

  // Initialise TinyUSB host stack
  tuh_arduino_init();

  ARDUINO_TINYUSB_CONSOLE.println(F("Waiting for printer...\n"));
}

void loop() {
  // Run the TinyUSB host task — drives enumeration & callbacks
  tuh_arduino_poll();
}
