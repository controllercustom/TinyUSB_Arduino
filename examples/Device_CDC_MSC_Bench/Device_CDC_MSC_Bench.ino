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
// Device_CDC_MSC_Bench.ino — minimal CDC+MSC bulk-throughput meter (device mode).
//
// Ported from TinyUSB examples/device/cdc_msc_throughput. No backing storage:
// MSC writes are discarded, reads zero-fill only the head LBAs the host scans
// during enumeration, and the CDC path drains RX while sourcing TX from a
// static filler — `dd` numbers reflect the USB/driver ceiling, not storage.
//
// Test: dd if=/dev/zero of=/dev/ttyACM0 / dd of=/dev/null if=/dev/ttyACM0,
// or dd against the MSC block device.
//
// Works on ALL boards (ArduinoTinyUSB multi-board library).
// Debug: ARDUINO_TINYUSB_CONSOLE @ ARDUINO_TINYUSB_BAUD.
//
// Adaptations:
// - Descriptors in the usb_descriptors.cpp tab (the .ino prototype
//   generator mangles extern "C").
// - PID 0x4013 (upstream 0x4003 collides with the Device_CDC_MSC port's PID).
// - MSC bulk endpoints use distinct physical EP numbers (OUT 0x03, IN 0x84):
//   the SAM3X DEVEPTCFG EPDIR bit is write-once in silicon, so the bulk OUT
//   and IN must live on distinct physical endpoints.
// - LED blink via LED_BUILTIN + millis().

#include "ArduinoTinyUSB.h"

#define USB_VID 0xCafe
#define USB_PID 0x4013

enum
{
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500
};

uint32_t blinkIntervalMs = BLINK_NOT_MOUNTED;

static void cdcThroughputTask(void)
{
  if (!tud_cdc_connected())
  {
    return;
  }

  static uint8_t const filler[CFG_TUD_CDC_TX_EPSIZE] = { 0 };
  uint32_t room = tud_cdc_write_available();
  while (room > 0)
  {
    uint32_t n = tud_cdc_write(filler, tu_min32(room, sizeof(filler)));
    if (n == 0)
    {
      break;
    }
    room -= n;
  }
  tud_cdc_write_flush();
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

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it, avoiding a mid-enumeration
  // device swap that wedges the host port.
  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[CDC_MSC_Bench] ArduinoTinyUSB bulk throughput meter");
  ARDUINO_TINYUSB_CONSOLE.println("[CDC_MSC_Bench] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  ledBlinkingTask();
  cdcThroughputTask();
}
