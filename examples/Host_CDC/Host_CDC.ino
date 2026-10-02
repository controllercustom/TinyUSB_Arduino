/*
 * Host_CDC - USB Host CDC (Serial) Example
 *
 * Demonstrates using the board as a USB host to communicate
 * with a CDC-ACM device (e.g., another Arduino acting as USB serial).
 *
 * Connect a USB CDC device to the board's USB host port.
 * Received serial data is printed to the TinyUSB console.
 */

#include "ArduinoTinyUSB.h"

static uint8_t cdc_idx = 0;
static bool cdc_connected = false;

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(115200);
  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[CDC] TinyUSB Host CDC ready");
  tuh_arduino_init();
}

void loop() {
  tuh_arduino_poll();

  if (cdc_connected) {
    uint8_t buf[256];
    uint32_t count = tuh_cdc_read(cdc_idx, buf, sizeof(buf) - 1);
    if (count > 0) {
      buf[count] = '\0';
      ARDUINO_TINYUSB_CONSOLE.print(F("[RX] "));
      ARDUINO_TINYUSB_CONSOLE.println((char *)buf);
    }

    // Bridge: forward console input to the CDC device
    if (ARDUINO_TINYUSB_CONSOLE.available()) {
      uint8_t txBuf[256];
      uint32_t txLen = 0;
      while (ARDUINO_TINYUSB_CONSOLE.available() && txLen < sizeof(txBuf) - 1) {
        txBuf[txLen++] = ARDUINO_TINYUSB_CONSOLE.read();
      }
      uint32_t written = tuh_cdc_write(cdc_idx, txBuf, txLen);
      tuh_cdc_write_flush(cdc_idx);
      ARDUINO_TINYUSB_CONSOLE.print(F("[TX] "));
      ARDUINO_TINYUSB_CONSOLE.print(written);
      ARDUINO_TINYUSB_CONSOLE.println(F(" bytes"));
    }
  }
}

extern "C" {

// Called when a CDC device is mounted (plugged in)
void tuh_cdc_mount_cb(uint8_t idx) {
  cdc_idx = idx;
  cdc_connected = true;
  ARDUINO_TINYUSB_CONSOLE.print("[CDC] Device mounted, idx=");
  ARDUINO_TINYUSB_CONSOLE.println(idx);
}

// Called when a CDC device is unmounted (unplugged)
void tuh_cdc_umount_cb(uint8_t idx) {
  if (idx == cdc_idx) {
    cdc_connected = false;
  }
  ARDUINO_TINYUSB_CONSOLE.print("[CDC] Device unmounted, idx=");
  ARDUINO_TINYUSB_CONSOLE.println(idx);
}

} // extern "C"
