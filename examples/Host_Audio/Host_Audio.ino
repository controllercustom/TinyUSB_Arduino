// Host_Audio.ino — Receives audio from a USB UAC2 microphone.
//
// Plug a USB audio device (UAC2 microphone) into the host port.
// The sketch enumerates it, selects the first supported capture
// configuration, starts streaming, and prints audio level info.
//
// Requires the audio host driver (stream API), which the vendored TinyUSB
// tree provides.

#include "ArduinoTinyUSB.h"

// ── Audio state ─────────────────────────────────────────────────────

static bool     audio_active = false;
static uint8_t  audio_dev    = 0;
static uint8_t  audio_stream = 0;
static uint32_t audio_sample_rate = 0;
static uint8_t  audio_channels    = 0;
static uint8_t  audio_bytes_per_sample = 0;

// ── Format name helper ─────────────────────────────────────────────

static const char *format_name(tuh_audio_format_t fmt) {
  switch (fmt) {
    case TUH_AUDIO_FORMAT_S8:      return "S8";
    case TUH_AUDIO_FORMAT_S16_LE:  return "S16_LE";
    case TUH_AUDIO_FORMAT_S24_3LE: return "S24_3LE";
    case TUH_AUDIO_FORMAT_S24_LE:  return "S24_LE";
    case TUH_AUDIO_FORMAT_S32_LE:  return "S32_LE";
    default:                       return "Unknown";
  }
}

// ── Start capture on the first available capture stream ─────────────

static void start_capture(uint8_t idx) {
  uint8_t stream_count = tuh_audio_stream_count(idx);

  for (uint8_t s = 0; s < stream_count; s++) {
    if (!tuh_audio_stream_exists(idx, s)) continue;
    if (tuh_audio_stream_direction(idx, s) != TUH_AUDIO_STREAM_CAPTURE) continue;

    uint8_t config_count = tuh_audio_config_count(idx, s);
    if (config_count == 0) {
      ARDUINO_TINYUSB_CONSOLE.println(F("  No capture configurations available."));
      continue;
    }

    // Pick the first configuration
    tuh_audio_stream_config_t cfg;
    if (!tuh_audio_config_get(idx, s, 0, &cfg)) continue;

    ARDUINO_TINYUSB_CONSOLE.print(F("  Config[0]: "));
    ARDUINO_TINYUSB_CONSOLE.print(cfg.channels);
    ARDUINO_TINYUSB_CONSOLE.print(F("ch "));
    ARDUINO_TINYUSB_CONSOLE.print(format_name(cfg.format));
    ARDUINO_TINYUSB_CONSOLE.print(F(" @ "));
    ARDUINO_TINYUSB_CONSOLE.print(cfg.sample_rate);
    ARDUINO_TINYUSB_CONSOLE.println(F(" Hz"));

    if (tuh_audio_configure(idx, s, 0)) {
      if (tuh_audio_start(idx, s)) {
        audio_dev    = idx;
        audio_stream = s;
        audio_sample_rate    = cfg.sample_rate;
        audio_channels       = cfg.channels;
        audio_bytes_per_sample = tuh_audio_format_bytes(cfg.format);
        ARDUINO_TINYUSB_CONSOLE.println(F("  Streaming started, waiting for audio data..."));
      } else {
        ARDUINO_TINYUSB_CONSOLE.println(F("  Failed to start stream."));
      }
    } else {
      ARDUINO_TINYUSB_CONSOLE.println(F("  Failed to configure stream."));
    }

    return;  // Use only the first capture stream
  }

  ARDUINO_TINYUSB_CONSOLE.println(F("  No capture streams found."));
}

// ── TinyUSB Host Audio callbacks ───────────────────────────────────

extern "C" void tuh_audio_mount_cb(uint8_t idx) {
  ARDUINO_TINYUSB_CONSOLE.print(F("\n── Audio Mounted ── idx="));
  ARDUINO_TINYUSB_CONSOLE.println(idx);

  uint8_t stream_count = tuh_audio_stream_count(idx);
  ARDUINO_TINYUSB_CONSOLE.print(F("  Streams: "));
  ARDUINO_TINYUSB_CONSOLE.println(stream_count);

  for (uint8_t s = 0; s < stream_count; s++) {
    if (!tuh_audio_stream_exists(idx, s)) continue;
    tuh_audio_direction_t dir = tuh_audio_stream_direction(idx, s);
    ARDUINO_TINYUSB_CONSOLE.print(F("  Stream "));
    ARDUINO_TINYUSB_CONSOLE.print(s);
    ARDUINO_TINYUSB_CONSOLE.print(F(": "));
    ARDUINO_TINYUSB_CONSOLE.println(dir == TUH_AUDIO_STREAM_CAPTURE ? F("Capture") : F("Playback"));
  }

  start_capture(idx);
}

extern "C" void tuh_audio_umount_cb(uint8_t idx) {
  ARDUINO_TINYUSB_CONSOLE.print(F("\n── Audio Removed ── idx="));
  ARDUINO_TINYUSB_CONSOLE.println(idx);

  if (audio_dev == idx) {
    audio_active = false;
    audio_dev = 0;
  }

  ARDUINO_TINYUSB_CONSOLE.println(F("Waiting for audio devices...\n"));
}

extern "C" void tuh_audio_capture_cb(uint8_t idx, uint8_t stream_idx, uint16_t xferred_bytes) {
  (void)idx;
  (void)stream_idx;

  if (xferred_bytes == 0) return;

  // Read available frames from the capture FIFO
  uint32_t available = tuh_audio_read_available(idx, stream_idx);
  if (available == 0) return;

  // Limit read to a reasonable chunk to avoid buffer issues
  uint32_t frame_size = audio_bytes_per_sample * audio_channels;
  if (frame_size == 0) return;

  uint32_t max_frames = 480;  // ~10 ms at 48 kHz
  uint32_t to_read = available < max_frames ? available : max_frames;

  // Read into a stack buffer (safe for moderate sizes)
  uint8_t buf[480 * 4];  // worst case: 480 frames * 4 bytes/sample
  uint32_t bytes_needed = to_read * frame_size;
  if (bytes_needed > sizeof(buf)) {
    to_read = sizeof(buf) / frame_size;
    bytes_needed = to_read * frame_size;
  }

  uint32_t frames_read = tuh_audio_read(idx, stream_idx, buf, to_read);
  if (frames_read == 0) return;

  // Compute RMS level for a simple VU meter
  int64_t sum_squares = 0;
  uint32_t total_samples = frames_read * audio_channels;

  for (uint32_t i = 0; i < total_samples; i++) {
    int32_t sample = 0;
    switch (audio_bytes_per_sample) {
      case 1: sample = (int8_t)buf[i]; break;
      case 2: sample = (int16_t)(buf[i*2] | (buf[i*2+1] << 8)); break;
      case 3: {
        uint32_t v = buf[i*3] | (buf[i*3+1] << 8) | (buf[i*3+2] << 16);
        if (v & 0x800000) v |= 0xFF000000;
        sample = (int32_t)v;
        break;
      }
      case 4: sample = (int32_t)(buf[i*4] | (buf[i*4+1] << 8) |
                                  (buf[i*4+2] << 16) | (buf[i*4+3] << 24));
              break;
    }
    sum_squares += (int64_t)sample * sample;
  }

  // Print level periodically (every ~100 ms worth of data)
  static uint32_t last_print = 0;
  uint32_t now = millis();
  if (now - last_print >= 100) {
    last_print = now;

    // Normalize to 0.0–1.0 range
    float rms = 0.0f;
    if (total_samples > 0) {
      float mean_sq = (float)sum_squares / total_samples;
      rms = sqrtf(mean_sq);
    }

    // Scale to percentage of full scale
    float full_scale = (1 << (audio_bytes_per_sample * 8 - 1)) - 1;
    float level_pct = (rms / full_scale) * 100.0f;

    ARDUINO_TINYUSB_CONSOLE.print(F("Audio Level: "));
    ARDUINO_TINYUSB_CONSOLE.print(level_pct, 1);
    ARDUINO_TINYUSB_CONSOLE.print(F("%  frames="));
    ARDUINO_TINYUSB_CONSOLE.println(frames_read);
  }
}

extern "C" void tuh_audio_event_cb(uint8_t idx, uint8_t stream_idx,
                                   tuh_audio_event_t event, tusb_xfer_result_t result) {
  switch (event) {
    case TUH_AUDIO_EVENT_START_COMPLETE:
      if (result == XFER_RESULT_SUCCESS) {
        audio_active = true;
        ARDUINO_TINYUSB_CONSOLE.println(F("  Capture start complete."));
      } else {
        ARDUINO_TINYUSB_CONSOLE.print(F("  Capture start failed, result="));
        ARDUINO_TINYUSB_CONSOLE.println(result);
      }
      break;

    case TUH_AUDIO_EVENT_STOP_COMPLETE:
      audio_active = false;
      ARDUINO_TINYUSB_CONSOLE.println(F("  Capture stopped."));
      break;

    case TUH_AUDIO_EVENT_XFER_FAILED:
      ARDUINO_TINYUSB_CONSOLE.print(F("  Transfer error on stream "));
      ARDUINO_TINYUSB_CONSOLE.print(stream_idx);
      ARDUINO_TINYUSB_CONSOLE.print(F(", result="));
      ARDUINO_TINYUSB_CONSOLE.println(result);
      break;
  }
}

// ── Arduino setup / loop ────────────────────────────────────────────

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(115200);
  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { /* wait for Serial */ }

  ARDUINO_TINYUSB_CONSOLE.println(F("\n=== TinyUSB Host Audio (UAC2 Capture) ==="));

  // Initialise TinyUSB host stack
  tuh_arduino_init();

  ARDUINO_TINYUSB_CONSOLE.println(F("Waiting for audio devices...\n"));
}

void loop() {
  // Run the TinyUSB host task — drives enumeration & callbacks
  tuh_arduino_poll();
}
