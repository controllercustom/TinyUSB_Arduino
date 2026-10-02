// Device_Audio.ino — UAC2 microphone that sends a sine wave tone.
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC

#include <ArduinoTinyUSB.h>

// Audio parameters — match tusb_config.h values
#define SAMPLE_RATE   48000
#define N_BYTES       2     // 16-bit samples
#define N_CHANNELS    1

// EP size: one USB full-speed frame (1 ms) of 16-bit mono 48 kHz audio
#define EP_SZ_IN      ((SAMPLE_RATE / 1000 + 1) * N_BYTES * N_CHANNELS)  // 96 bytes

// Sine-wave tone generator (~440 Hz)
static uint32_t phase = 0;
static const uint32_t PHASE_INC = (440 * 65536) / SAMPLE_RATE;  // 16.16 fixed-point

static int16_t sine_next(void) {
  phase += PHASE_INC;
  float angle = (float)(phase & 0xFFFF) / 65536.0f * 2.0f * PI;
  return (int16_t)(sinf(angle) * 16000);
}

// Sample buffer — enough for one USB frame
static int16_t buf[EP_SZ_IN / N_BYTES];

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it.
  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[Audio] Arduino-TinyUSB UAC2 Microphone");
  ARDUINO_TINYUSB_CONSOLE.print("[Audio] Sample rate: ");
  ARDUINO_TINYUSB_CONSOLE.print(SAMPLE_RATE);
  ARDUINO_TINYUSB_CONSOLE.println(" Hz");

  ARDUINO_TINYUSB_CONSOLE.println("[Audio] Device stack initialized.");
}

void loop() {
  // Feed the TinyUSB device task
  tud_arduino_task();

  // Only send audio when the host has opened the streaming interface
  if (!tud_audio_mounted()) return;

  // Fill buffer with one USB frame (1 ms) of sine-wave samples
  uint16_t n_samples = EP_SZ_IN / N_BYTES;
  for (uint16_t i = 0; i < n_samples; i++) {
    buf[i] = sine_next();
  }

  // Write audio data into the EP IN FIFO
  tud_audio_write(buf, sizeof(buf));
}
