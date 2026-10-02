/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
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
// Device_MIDI_Seq.ino — USB MIDI note sequencer (ArduinoTinyUSB multi-board).
// Upstream: TinyUSB examples/device/midi_test (main.c + usb_descriptors.c).
// Sends a repeating note sequence; incoming MIDI packets are read and
// discarded so the sender never blocks. Test on PC with a synth
// (Linux: qsynth+qjackctl, Windows: MIDI-OX, macOS: SimpleSynth).
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC

#include "ArduinoTinyUSB.h"

#define USB_VID 0xCafe
#define USB_PID 0x4014

extern uint32_t blinkIntervalMs;

static uint32_t notePos = 0;

static uint8_t const noteSequence[] =
{
  74, 78, 81, 86, 90, 93, 98, 102, 57, 61, 66, 69, 73, 78, 81, 85,
  88, 92, 97, 100, 97, 92, 88, 85, 81, 78, 74, 69, 66, 62, 57, 62,
  66, 69, 74, 78, 81, 86, 90, 93, 97, 102, 97, 93, 90, 85, 81, 78,
  73, 68, 64, 61, 56, 61, 64, 68, 74, 78, 81, 86, 90, 93, 98, 102
};

static void midiTask(void);
static void ledBlinkingTask(void);

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[MIDI_Seq] ArduinoTinyUSB USB MIDI sequencer (cafe:4014)");
  ARDUINO_TINYUSB_CONSOLE.println("[MIDI_Seq] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  ledBlinkingTask();
  midiTask();
}

static void midiTask(void)
{
  static uint32_t startMs = 0;

  uint8_t const cableNum = 0;
  uint8_t const channel = 0;

  while (tud_midi_available())
  {
    uint8_t packet[4];
    tud_midi_packet_read(packet);
  }

  if (millis() - startMs < 286)
  {
    return;
  }
  startMs += 286;

  int previous = (int) (notePos - 1);
  if (previous < 0)
  {
    previous = sizeof(noteSequence) - 1;
  }

  uint8_t noteOn[3] = { (uint8_t) (0x90 | channel), noteSequence[notePos], 127 };
  tud_midi_stream_write(cableNum, noteOn, 3);

  uint8_t noteOff[3] = { (uint8_t) (0x80 | channel), noteSequence[previous], 0 };
  tud_midi_stream_write(cableNum, noteOff, 3);

  notePos++;
  if (notePos >= sizeof(noteSequence))
  {
    notePos = 0;
  }
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
