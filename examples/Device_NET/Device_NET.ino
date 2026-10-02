// Device_NET.ino — CDC-NCM network device (Ethernet-over-USB).
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC
//
// On the host side this appears as a network adapter (NCM class).
// Received Ethernet frames are echoed back as-is (loopback),
// demonstrating a minimal USB network device.

#include <ArduinoTinyUSB.h>

// ─── MAC address for this device ───
uint8_t tud_network_mac_address[6] = { 0x02, 0x02, 0x84, 0x6A, 0x96, 0x00 };

// ─── Receive buffer ───
#ifndef CFG_TUD_NET_MTU
  #define CFG_TUD_NET_MTU 1514
#endif

static bool net_link_up = false;

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it.
  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[NET] Arduino-TinyUSB CDC-NCM Network");

  ARDUINO_TINYUSB_CONSOLE.println("[NET] Device stack initialized.");
  ARDUINO_TINYUSB_CONSOLE.print("[NET] MAC: ");
  for (uint8_t i = 0; i < 6; i++) {
    if (tud_network_mac_address[i] < 0x10) ARDUINO_TINYUSB_CONSOLE.print('0');
    ARDUINO_TINYUSB_CONSOLE.print(tud_network_mac_address[i], HEX);
    if (i < 5) ARDUINO_TINYUSB_CONSOLE.print(':');
  }
  ARDUINO_TINYUSB_CONSOLE.println();
}

void loop() {
  // Feed the TinyUSB device task
  tud_arduino_task();

  // Bring link up after enumeration
  if (!net_link_up && tud_connected()) {
    tud_network_link_state(0, true);
    net_link_up = true;
    ARDUINO_TINYUSB_CONSOLE.println("[NET] Link UP");
  }
}

// ─── Network callbacks required by TinyUSB NCM ───

// Called when the host sends a packet to us
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  (void) src;
  (void) size;
  // Simple loopback: accept all frames, then renew so stack can receive more
  tud_network_recv_renew();
  return true;
}

// Called to copy an outgoing packet into the transmit buffer
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  (void) dst;
  (void) ref;
  (void) arg;
  // Nothing to transmit in this echo-only example
  return 0;
}

// Called when the host resets the network interface
void tud_network_init_cb(void) {
  net_link_up = false;
  ARDUINO_TINYUSB_CONSOLE.println("[NET] Host reset network interface");
}
