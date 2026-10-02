// Host_MIDI.ino — Reads MIDI messages from a USB MIDI device.
//
// Plug a USB MIDI device (keyboard, controller, etc.) into the
// host port.  The sketch enumerates it and prints every MIDI
// message it receives, decoded into human-readable form.
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC

#include "ArduinoTinyUSB.h"

// ── MIDI Note Name Helper ──────────────────────────────────────────

static const char *note_name(uint8_t note) {
  static const char *names[] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
  };
  static char buf[5];
  uint8_t octave = (note / 12) - 1;
  if (octave > 9) octave = 9;
  if (octave < 0) octave = -1;
  const char *name = names[note % 12];
  if (octave >= 0) {
    buf[0] = name[0];
    buf[1] = name[1] ? name[1] : ' ';
    buf[2] = '0' + octave;
    buf[3] = '\0';
  } else {
    buf[0] = name[0];
    buf[1] = name[1] ? name[1] : ' ';
    buf[2] = '-';
    buf[3] = '1';
    buf[4] = '\0';
  }
  return buf;
}

// ── TinyUSB Host MIDI callbacks ────────────────────────────────────

extern "C" void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_cb_data) {
  ARDUINO_TINYUSB_CONSOLE.print(F("\n── MIDI Mounted ──\n"));
  ARDUINO_TINYUSB_CONSOLE.print(F("  idx="));     ARDUINO_TINYUSB_CONSOLE.print(idx);
  ARDUINO_TINYUSB_CONSOLE.print(F("  addr="));    ARDUINO_TINYUSB_CONSOLE.print(mount_cb_data->daddr);
  ARDUINO_TINYUSB_CONSOLE.print(F("  itf="));     ARDUINO_TINYUSB_CONSOLE.print(mount_cb_data->bInterfaceNumber);
  ARDUINO_TINYUSB_CONSOLE.print(F("  rx_cables=")); ARDUINO_TINYUSB_CONSOLE.print(mount_cb_data->rx_cable_count);
  ARDUINO_TINYUSB_CONSOLE.print(F("  tx_cables=")); ARDUINO_TINYUSB_CONSOLE.println(mount_cb_data->tx_cable_count);
  ARDUINO_TINYUSB_CONSOLE.println(F("Waiting for MIDI data...\n"));
}

extern "C" void tuh_midi_umount_cb(uint8_t idx) {
  ARDUINO_TINYUSB_CONSOLE.print(F("\n── MIDI Removed ── idx="));
  ARDUINO_TINYUSB_CONSOLE.println(idx);
  ARDUINO_TINYUSB_CONSOLE.println(F("Waiting for MIDI devices...\n"));
}

extern "C" void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes) {
  uint8_t packet[4];

  while (tuh_midi_packet_read(idx, packet)) {
    uint8_t cin = packet[0] & 0x0F;
    uint8_t cable = (packet[0] >> 4) & 0x0F;
    uint8_t status = packet[1];
    uint8_t channel = (status & 0x0F) + 1;
    uint8_t data1 = packet[2];
    uint8_t data2 = packet[3];

    ARDUINO_TINYUSB_CONSOLE.print(F("[cable="));
    ARDUINO_TINYUSB_CONSOLE.print(cable);
    ARDUINO_TINYUSB_CONSOLE.print(F("] "));

    switch (cin) {
      case 0x8: // Note Off
        ARDUINO_TINYUSB_CONSOLE.print(F("Note Off   ch="));
        ARDUINO_TINYUSB_CONSOLE.print(channel);
        ARDUINO_TINYUSB_CONSOLE.print(F(" note="));
        ARDUINO_TINYUSB_CONSOLE.print(data1);
        ARDUINO_TINYUSB_CONSOLE.print(F("("));
        ARDUINO_TINYUSB_CONSOLE.print(note_name(data1));
        ARDUINO_TINYUSB_CONSOLE.print(F(") vel="));
        ARDUINO_TINYUSB_CONSOLE.println(data2);
        break;

      case 0x9: // Note On
        ARDUINO_TINYUSB_CONSOLE.print(F("Note On    ch="));
        ARDUINO_TINYUSB_CONSOLE.print(channel);
        ARDUINO_TINYUSB_CONSOLE.print(F(" note="));
        ARDUINO_TINYUSB_CONSOLE.print(data1);
        ARDUINO_TINYUSB_CONSOLE.print(F("("));
        ARDUINO_TINYUSB_CONSOLE.print(note_name(data1));
        ARDUINO_TINYUSB_CONSOLE.print(F(") vel="));
        ARDUINO_TINYUSB_CONSOLE.println(data2);
        break;

      case 0xA: // Poly Keypress
        ARDUINO_TINYUSB_CONSOLE.print(F("Poly Key   ch="));
        ARDUINO_TINYUSB_CONSOLE.print(channel);
        ARDUINO_TINYUSB_CONSOLE.print(F(" note="));
        ARDUINO_TINYUSB_CONSOLE.print(data1);
        ARDUINO_TINYUSB_CONSOLE.print(F(" press="));
        ARDUINO_TINYUSB_CONSOLE.println(data2);
        break;

      case 0xB: // Control Change
        ARDUINO_TINYUSB_CONSOLE.print(F("CC         ch="));
        ARDUINO_TINYUSB_CONSOLE.print(channel);
        ARDUINO_TINYUSB_CONSOLE.print(F(" cc="));
        ARDUINO_TINYUSB_CONSOLE.print(data1);
        ARDUINO_TINYUSB_CONSOLE.print(F(" val="));
        ARDUINO_TINYUSB_CONSOLE.println(data2);
        break;

      case 0xC: // Program Change
        ARDUINO_TINYUSB_CONSOLE.print(F("Prog Chg   ch="));
        ARDUINO_TINYUSB_CONSOLE.print(channel);
        ARDUINO_TINYUSB_CONSOLE.print(F(" prog="));
        ARDUINO_TINYUSB_CONSOLE.println(data1);
        break;

      case 0xD: // Channel Pressure (Aftertouch)
        ARDUINO_TINYUSB_CONSOLE.print(F("Aftertouch ch="));
        ARDUINO_TINYUSB_CONSOLE.print(channel);
        ARDUINO_TINYUSB_CONSOLE.print(F(" press="));
        ARDUINO_TINYUSB_CONSOLE.println(data1);
        break;

      case 0xE: // Pitch Bend
        {
          int16_t bend = (int16_t)((data2 << 7) | data1) - 8192;
          ARDUINO_TINYUSB_CONSOLE.print(F("Pitch Bend ch="));
          ARDUINO_TINYUSB_CONSOLE.print(channel);
          ARDUINO_TINYUSB_CONSOLE.print(F(" val="));
          ARDUINO_TINYUSB_CONSOLE.println(bend);
        }
        break;

      case 0x4: // SysEx start / continue
      case 0x5: // SysEx end with 1 byte
      case 0x6: // SysEx end with 2 bytes
      case 0x7: // SysEx end with 3 bytes
        ARDUINO_TINYUSB_CONSOLE.print(F("SysEx      "));
        ARDUINO_TINYUSB_CONSOLE.print(F("cin=0x"));
        ARDUINO_TINYUSB_CONSOLE.print(cin, HEX);
        ARDUINO_TINYUSB_CONSOLE.print(F(" data=0x"));
        if (data1 < 0x10) ARDUINO_TINYUSB_CONSOLE.print('0');
        ARDUINO_TINYUSB_CONSOLE.print(data1, HEX);
        ARDUINO_TINYUSB_CONSOLE.print(F(" 0x"));
        if (data2 < 0x10) ARDUINO_TINYUSB_CONSOLE.print('0');
        ARDUINO_TINYUSB_CONSOLE.print(data2, HEX);
        if (cin >= 0x6 && packet[3] != 0xF7) {
          ARDUINO_TINYUSB_CONSOLE.print(F(" 0x"));
          if (packet[3] < 0x10) ARDUINO_TINYUSB_CONSOLE.print('0');
          ARDUINO_TINYUSB_CONSOLE.print(packet[3], HEX);
        }
        ARDUINO_TINYUSB_CONSOLE.println();
        break;

      case 0x2: // System Common: MTC Quarter Frame / Song Select
        ARDUINO_TINYUSB_CONSOLE.print(F("SysCommon  "));
        if (status == 0xF1) ARDUINO_TINYUSB_CONSOLE.print(F("MTC QF="));
        else if (status == 0xF3) ARDUINO_TINYUSB_CONSOLE.print(F("Song Sel="));
        ARDUINO_TINYUSB_CONSOLE.println(data1);
        break;

      case 0x3: // System Common: Song Position Pointer
        ARDUINO_TINYUSB_CONSOLE.print(F("SysCommon  SPP="));
        ARDUINO_TINYUSB_CONSOLE.println((data2 << 7) | data1);
        break;

      case 0xF: // Single byte (real-time, etc.)
        if (status == 0xF8) ARDUINO_TINYUSB_CONSOLE.println(F("Timing Clock"));
        else if (status == 0xFA) ARDUINO_TINYUSB_CONSOLE.println(F("Start"));
        else if (status == 0xFB) ARDUINO_TINYUSB_CONSOLE.println(F("Continue"));
        else if (status == 0xFC) ARDUINO_TINYUSB_CONSOLE.println(F("Stop"));
        else if (status == 0xFE) ARDUINO_TINYUSB_CONSOLE.println(F("Active Sensing"));
        else if (status == 0xFF) ARDUINO_TINYUSB_CONSOLE.println(F("System Reset"));
        else if (status == 0xF6) ARDUINO_TINYUSB_CONSOLE.println(F("Tune Request"));
        else {
          ARDUINO_TINYUSB_CONSOLE.print(F("SingleByte 0x"));
          ARDUINO_TINYUSB_CONSOLE.println(status, HEX);
        }
        break;

      default:
        ARDUINO_TINYUSB_CONSOLE.print(F("Unknown    cin=0x"));
        ARDUINO_TINYUSB_CONSOLE.print(cin, HEX);
        ARDUINO_TINYUSB_CONSOLE.print(F(" data=0x"));
        if (packet[1] < 0x10) ARDUINO_TINYUSB_CONSOLE.print('0');
        ARDUINO_TINYUSB_CONSOLE.print(packet[1], HEX);
        ARDUINO_TINYUSB_CONSOLE.print(F(" 0x"));
        if (packet[2] < 0x10) ARDUINO_TINYUSB_CONSOLE.print('0');
        ARDUINO_TINYUSB_CONSOLE.print(packet[2], HEX);
        ARDUINO_TINYUSB_CONSOLE.print(F(" 0x"));
        if (packet[3] < 0x10) ARDUINO_TINYUSB_CONSOLE.print('0');
        ARDUINO_TINYUSB_CONSOLE.println(packet[3], HEX);
        break;
    }
  }
}

// ── Arduino setup / loop ────────────────────────────────────────────

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(115200);
  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { /* wait for Serial */ }

  ARDUINO_TINYUSB_CONSOLE.println(F("\n=== TinyUSB Host MIDI ==="));

  // Initialise TinyUSB host stack
  tuh_arduino_init();

  ARDUINO_TINYUSB_CONSOLE.println(F("Waiting for MIDI devices...\n"));
}

void loop() {
  // Run the TinyUSB host task — drives enumeration & callbacks
  tuh_arduino_poll();
}
