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
// Device_CDC_MSC.ino — USB CDC echo + Mass Storage RAM disk (device mode).
//
// Ported from TinyUSB examples/device/cdc_msc. The CDC port echoes received
// data; the MSC interface exposes a 16-sector x 512B = 8KB FAT12 RAM disk
// (README.TXT).
//
// Works on ALL boards (ArduinoTinyUSB multi-board library).
// Debug: ARDUINO_TINYUSB_CONSOLE @ ARDUINO_TINYUSB_BAUD.
//
// Adaptations:
// - Descriptors in the usb_descriptors.cpp tab (the .ino prototype
//   generator mangles extern "C").
// - Upstream tud_cdc_line_state_cb() is DROPPED; the connect greeting uses
//   tud_cdc_n_connected() edge polling instead. The on-board-button
//   UART-state notification is dropped (no button).
// - MSC bulk endpoints use distinct physical EP numbers (OUT 0x03, IN 0x84):
//   the SAM3X DEVEPTCFG EPDIR bit is write-once in silicon, so sharing one
//   EP number for both directions (upstream 0x03/0x83) breaks the Due.
// - LED blink via LED_BUILTIN + millis().

#include "ArduinoTinyUSB.h"

#define USB_VID 0xCafe
#define USB_PID 0x4003

enum
{
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500
};

uint32_t blinkIntervalMs = BLINK_NOT_MOUNTED;
static bool cdcGreeted = false;

#define README_CONTENTS \
"This is tinyusb's MassStorage Class demo.\r\n\r\n\
If you find any bugs or get any questions, feel free to file an\r\n\
issue at github.com/hathach/tinyusb"

enum
{
  DISK_BLOCK_NUM = 16,
  DISK_BLOCK_SIZE = 512
};

// The initializers below only spell out the meaningful bytes; the rest of
// each 512B block is zero-filled by the compiler. The FAT boot signature
// (0x55AA at offset 510-511 of block 0) is patched in setup() below.
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

static void cdcTask(void)
{
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

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it, avoiding a mid-enumeration
  // device swap that wedges the host port.
  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[CDC_MSC] ArduinoTinyUSB CDC + MSC RAM disk");
  ARDUINO_TINYUSB_CONSOLE.println("[CDC_MSC] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  ledBlinkingTask();
  cdcTask();
}
