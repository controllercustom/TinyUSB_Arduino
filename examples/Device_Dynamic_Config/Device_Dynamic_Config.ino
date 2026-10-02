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
// Device_Dynamic_Config.ino — runtime-selected USB personality (device mode).
//
// Ported from TinyUSB examples/device/dynamic_configuration. Mode 0
// enumerates as CDC + MIDI note sequencer; mode 1 enumerates as an 8KB MSC
// RAM disk. The active config descriptor is assembled at runtime into a
// static buffer (see the usb_descriptors.cpp tab).
//
// Upstream picks the mode from the on-board button sampled during
// enumeration. There is no such button: send 'm' (MSC) or 'c' (CDC+MIDI)
// on the console, then RE-PLUG the USB cable (or reset the host port) so
// the host re-enumerates with the new descriptors.
//
// Works on ALL boards (ArduinoTinyUSB multi-board library).
// Debug/mode commands: ARDUINO_TINYUSB_CONSOLE @ ARDUINO_TINYUSB_BAUD.
//
// Adaptations:
// - Descriptors in the usb_descriptors.cpp tab (the .ino prototype
//   generator mangles extern "C").
//   The config buffer is pre-loaded with the mode-0 template at compile
//   time (the host may enumerate before setup() runs) and re-built by
//   memcpy when the mode changes — malloc-once equivalent with a static
//   buffer, no heap.
// - Upstream tud_cdc_line_state_cb() is DROPPED; the connect greeting uses
//   tud_cdc_n_connected() edge polling instead.
// - Mode-1 MSC bulk endpoints use distinct physical EP numbers (OUT 0x01,
//   IN 0x82): the SAM3X DEVEPTCFG EPDIR bit is write-once in silicon.
// - LED blink via LED_BUILTIN + millis().

#include "ArduinoTinyUSB.h"

#define USB_VID 0xCafe
#define USB_PID_MODE0 0x400B
#define USB_PID_MODE1 0x4016

enum
{
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500
};

uint32_t blinkIntervalMs = BLINK_NOT_MOUNTED;
static bool cdcGreeted = false;

uint32_t dynMode = 0;

// Helper owned by the usb_descriptors.cpp tab.
void rebuildConfig(void);

#define README_CONTENTS \
"This is tinyusb's MassStorage Class demo.\r\n\r\n\
If you find any bugs or get any questions, feel free to file an\r\n\
issue at github.com/hathach/tinyusb"

enum
{
  DISK_BLOCK_NUM = 16,
  DISK_BLOCK_SIZE = 512
};

uint8_t mscDisk[DISK_BLOCK_NUM][DISK_BLOCK_SIZE] =
{
  {
    0xEB, 0x3C, 0x90, 0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E, 0x30, 0x00, 0x02, 0x01, 0x01, 0x00,
    0x01, 0x10, 0x00, 0x10, 0x00, 0xF8, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x29, 0x34, 0x12, 0x00, 0x00, 'T', 'i', 'n', 'y', 'U',
    'S', 'B', ' ', 'M', 'S', 'C', 0x46, 0x41, 0x54, 0x31, 0x32, 0x20, 0x20, 0x20, 0x00, 0x00
  },
  {
    0xF8, 0xFF, 0xFF, 0xFF, 0x0F
  },
  {
    'T', 'i', 'n', 'y', 'U', 'S', 'B', ' ', 'M', 'S', 'C', 0x08, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4F, 0x6D, 0x65, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    'R', 'E', 'A', 'D', 'M', 'E', ' ', ' ', 'T', 'X', 'T', 0x20, 0x00, 0xC6, 0x52, 0x6D,
    0x65, 0x43, 0x65, 0x43, 0x00, 0x00, 0x88, 0x6D, 0x65, 0x43, 0x02, 0x00,
    sizeof(README_CONTENTS) - 1, 0x00, 0x00, 0x00
  },
  README_CONTENTS
};

static uint32_t notePos = 0;

static uint8_t const noteSequence[] =
{
  74, 78, 81, 86, 90, 93, 98, 102, 57, 61, 66, 69, 73, 78, 81, 85,
  88, 92, 97, 100, 97, 92, 88, 85, 81, 78, 74, 69, 66, 62, 57, 62,
  66, 69, 74, 78, 81, 86, 90, 93, 97, 102, 97, 93, 90, 85, 81, 78,
  73, 68, 64, 61, 56, 61, 64, 68, 74, 78, 81, 86, 90, 93, 98, 102
};

static void cdcTask(void)
{
  if (dynMode)
  {
    return;
  }

  if (tud_cdc_n_connected(0))
  {
    if (!cdcGreeted)
    {
      tud_cdc_write_str("\r\nTinyUSB CDC MSC device example\r\n");
      tud_cdc_write_flush();
      cdcGreeted = true;
    }

    if (tud_cdc_available())
    {
      uint8_t buf[64];
      uint32_t count = tud_cdc_read(buf, sizeof(buf));

      for (uint32_t i = 0; i < count; i++)
      {
        tud_cdc_write_char(buf[i]);
        if (buf[i] == '\r')
        {
          tud_cdc_write_char('\n');
        }
      }

      tud_cdc_write_flush();
    }
  }
  else
  {
    cdcGreeted = false;
  }
}

static void midiTask(void)
{
  static uint32_t startMs = 0;

  uint8_t const cableNum = 0;
  uint8_t const channel = 0;

  if (dynMode)
  {
    return;
  }

  uint8_t packet[4];
  while (tud_midi_available())
  {
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

static void modeTask(void)
{
  while (ARDUINO_TINYUSB_CONSOLE.available())
  {
    char c = (char) ARDUINO_TINYUSB_CONSOLE.read();
    if (c == 'm' || c == 'M')
    {
      dynMode = 1;
      rebuildConfig();
      ARDUINO_TINYUSB_CONSOLE.println("[dynamic_configuration] mode=1 (MSC). RE-PLUG USB to re-enumerate.");
    }
    else if (c == 'c' || c == 'C')
    {
      dynMode = 0;
      rebuildConfig();
      ARDUINO_TINYUSB_CONSOLE.println("[dynamic_configuration] mode=0 (CDC+MIDI). RE-PLUG USB to re-enumerate.");
    }
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

void setup()
{
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  pinMode(LED_BUILTIN, OUTPUT);

  mscDisk[0][510] = 0x55;
  mscDisk[0][511] = 0xAA;

  rebuildConfig();

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it, avoiding a mid-enumeration
  // device swap that wedges the host port.
  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[dynamic_configuration] ArduinoTinyUSB runtime USB personality");
  ARDUINO_TINYUSB_CONSOLE.println("[dynamic_configuration] Send 'm' for MSC, 'c' for CDC+MIDI, then RE-PLUG USB.");
  ARDUINO_TINYUSB_CONSOLE.println("[dynamic_configuration] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  ledBlinkingTask();
  modeTask();
  cdcTask();
  midiTask();
}
