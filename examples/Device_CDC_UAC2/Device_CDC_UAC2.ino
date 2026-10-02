/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Jerzy Kasenberg
 * Copyright (c) 2022 Angel Molina <angelmolinu@gmail.com>
 * Copyright (c) 2023 Dhiru Kholia <dhiru.kholia@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
// Device_CDC_UAC2.ino — USB CDC echo + UAC2 microphone (device mode).
//
// Ported from TinyUSB examples/device/cdc_uac2. The CDC port echoes received
// data; the audio function streams a 16-bit test ramp like Device_Audio.
//
// GAP vs upstream: upstream is a stereo HEADSET (speaker OUT + mic IN, two
// formats, 44.1/48 kHz) whose audio_task loops speaker data back into the
// mic. The configs enable EP IN only (mono 16-bit 48 kHz, no
// CFG_TUD_AUDIO_ENABLE_EP_OUT, no RX-path macros), so the speaker leg and
// the loopback cannot be built — do NOT edit the configs to add them.
// This port keeps the CDC side whole and the mic-IN side at the fixed
// format; the speaker path is compiled out and documented here.
//
// Works on ALL boards (ArduinoTinyUSB multi-board library).
// Debug: ARDUINO_TINYUSB_CONSOLE @ ARDUINO_TINYUSB_BAUD.
//
// Adaptations:
// - Descriptors + UAC2 entity callbacks in the usb_descriptors.cpp tab
//   (the .ino prototype generator mangles extern "C").
// - Upstream tud_cdc_line_state_cb() is DROPPED; the CDC echo runs on
//   tud_cdc_n_connected() polling.
// - CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE is defined sketch-locally when the
//   unified config does not provide it.
// - LED blink via LED_BUILTIN + millis().

#include "ArduinoTinyUSB.h"

#define USB_VID 0xCafe
#define USB_PID 0x4011

#define AUDIO_SAMPLE_RATE 48000

// Defined locally only when the unified config does not provide it.
#ifndef CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE
#define CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE AUDIO_SAMPLE_RATE
#endif

enum
{
  BLINK_STREAMING = 25,
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500
};

uint32_t blinkIntervalMs = BLINK_NOT_MOUNTED;
static bool cdcGreeted = false;

// UAC2 entity state, shared with the usb_descriptors.cpp callbacks.
bool mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
uint16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
uint32_t sampFreq;
uint8_t clkValid;

static uint16_t testBufferAudio[CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE / 1000 * CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX * CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX / 2];
uint16_t startVal = 0;

static void cdcTask(void)
{
  if (tud_cdc_n_connected(0))
  {
    if (!cdcGreeted)
    {
      tud_cdc_write_str("\r\nTinyUSB CDC + UAC2 mic example\r\n");
      tud_cdc_write_flush();
      cdcGreeted = true;
    }

    if (tud_cdc_available())
    {
      char buf[64];
      uint32_t count = tud_cdc_read(buf, sizeof(buf));

      tud_cdc_write(buf, count);
      tud_cdc_write_flush();
    }
  }
  else
  {
    cdcGreeted = false;
  }
}

static void audioTask(void)
{
  static uint32_t startMs = 0;
  uint32_t currMs = millis();
  if (startMs == currMs)
  {
    return;
  }
  startMs = currMs;
  for (size_t cnt = 0; cnt < sizeof(testBufferAudio) / 2; cnt++)
  {
    testBufferAudio[cnt] = startVal++;
  }
  tud_audio_write((uint8_t *) testBufferAudio, sizeof(testBufferAudio));
}

static void ledBlinkingTask(void)
{
  static uint32_t startMs = 0;
  static bool ledState = false;

  if (millis() - startMs < blinkIntervalMs)
  {
    return;
  }
  startMs += blinkIntervalMs;

  digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  ledState = !ledState;
}

void setup()
{
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  pinMode(LED_BUILTIN, OUTPUT);

  sampFreq = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
  clkValid = 1;

  (void) clkValid;

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it, avoiding a mid-enumeration
  // device swap that wedges the host port.
  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[CDC_UAC2] ArduinoTinyUSB CDC + UAC2 mic (speaker path unavailable, see header)");
  ARDUINO_TINYUSB_CONSOLE.println("[CDC_UAC2] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  ledBlinkingTask();
  cdcTask();
  audioTask();
}
