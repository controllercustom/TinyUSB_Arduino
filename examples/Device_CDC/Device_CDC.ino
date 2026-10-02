// Device_CDC.ino — CDC serial echo (tiny echo server on USB CDC).
//
// Works on ALL boards:
//   Due/Zero/GIGA: TinyUSB provides CDC on native USB; ARDUINO_TINYUSB_CONSOLE
//                   defaults to Serial1 (UART) for debug to avoid SAM core USB conflict.

#include <ArduinoTinyUSB.h>

// Packet buffers for tinyusb CDC
#ifndef CFG_TUD_CDC_RX_BUFSIZE
  #define CFG_TUD_CDC_RX_BUFSIZE 64
#endif
#ifndef CFG_TUD_CDC_TX_BUFSIZE
  #define CFG_TUD_CDC_TX_BUFSIZE 64
#endif

uint8_t buf_rx[CFG_TUD_CDC_RX_BUFSIZE];
uint8_t buf_tx[CFG_TUD_CDC_TX_BUFSIZE];

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  // Initialize TinyUSB device stack on the native USB port FIRST
  tud_arduino_init();

  ARDUINO_TINYUSB_CONSOLE.println("[CDC] Arduino-TinyUSB CDC echo");
  ARDUINO_TINYUSB_CONSOLE.println("[CDC] Type characters on USB CDC; they echo back.");
}

void loop() {
  // Feed the TinyUSB device task
  tud_arduino_task();

  // Echo data received from USB CDC back to the host
  uint32_t count = tud_cdc_n_available(0);
  if (count) {
    uint32_t read = tud_cdc_n_read(0, buf_rx, sizeof(buf_rx));
    for (uint32_t i = 0; i < read; i++) {
      if (buf_rx[i] == '\r') {
        buf_tx[0] = '\r'; buf_tx[1] = '\n';
        tud_cdc_n_write(0, buf_tx, 2);
      } else if (buf_rx[i] == '\b' || buf_rx[i] == 0x7F) {
        // Backspace: send " \b" to erase last char on most terminals
        buf_tx[0] = ' '; buf_tx[1] = '\b';
        tud_cdc_n_write(0, buf_tx, 2);
      } else {
        tud_cdc_n_write(0, &buf_rx[i], 1);
      }
    }
    tud_cdc_n_write_flush(0);
  }
}
