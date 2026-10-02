// Device_MIDI.ino — MIDI loopback that sends note-on/off messages.
//
// Sends note-on (C4, velocity 100) every 500 ms, note-off after 250 ms,
// and echoes any received MIDI data back to the host.
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC

#include <ArduinoTinyUSB.h>

// MIDI timing
static uint32_t prev_ms = 0;
static bool note_on = false;

// Note parameters
static const uint8_t MIDI_CHANNEL = 0;
static const uint8_t MIDI_NOTE = 0x3C;  // Middle C (C4)
static const uint8_t MIDI_VELOCITY = 100;

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it.
  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[MIDI] Arduino-TinyUSB MIDI Loopback");

  ARDUINO_TINYUSB_CONSOLE.println("[MIDI] Device stack initialized.");
}

void loop() {
  tud_arduino_task();

  // Send MIDI note-on/off every 500 ms
  uint32_t now = millis();
  if (now - prev_ms >= 500) {
    prev_ms = now;

    if (!note_on) {
      // Note On: USB-MIDI packet = [CIN|cable, status, note, velocity]
      uint8_t packet[4] = { 0x09, (uint8_t)(0x90 | MIDI_CHANNEL), MIDI_NOTE, MIDI_VELOCITY };
      tud_midi_packet_write(packet);
      ARDUINO_TINYUSB_CONSOLE.println("[MIDI] Note ON");
    } else {
      // Note Off: USB-MIDI packet = [CIN|cable, status, note, velocity]
      uint8_t packet[4] = { 0x08, (uint8_t)(0x80 | MIDI_CHANNEL), MIDI_NOTE, 0 };
      tud_midi_packet_write(packet);
      ARDUINO_TINYUSB_CONSOLE.println("[MIDI] Note OFF");
    }
    note_on = !note_on;
  }

  // Echo received MIDI packets back to host
  uint8_t rx_packet[4];
  while (tud_midi_packet_read(rx_packet)) {
    tud_midi_packet_write(rx_packet);
    ARDUINO_TINYUSB_CONSOLE.print("[MIDI] Echo: ");
    for (int i = 0; i < 4; i++) {
      ARDUINO_TINYUSB_CONSOLE.print("0x");
      if (rx_packet[i] < 0x10) ARDUINO_TINYUSB_CONSOLE.print("0");
      ARDUINO_TINYUSB_CONSOLE.print(rx_packet[i], HEX);
      if (i < 3) ARDUINO_TINYUSB_CONSOLE.print(" ");
    }
    ARDUINO_TINYUSB_CONSOLE.println();
  }
}
