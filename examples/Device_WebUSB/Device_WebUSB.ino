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
// Device_WebUSB.ino — CDC + WebUSB serial echo (device mode).
//
// Ported from TinyUSB examples/device/webusb_serial. After enumeration,
// open example.tinyusb.org/webusb-serial in Chrome, connect, and anything
// received on either the WebUSB (vendor) or CDC interface is echoed back
// to both. On Linux/macOS, udev rules may be needed for browser access;
// on Win7 and older, bind the WebUSB interface with Zadig (WinUSB).
//
// Works on ALL boards (ArduinoTinyUSB multi-board library).
// Debug: ARDUINO_TINYUSB_CONSOLE @ ARDUINO_TINYUSB_BAUD.
//
// Adaptations:
// - Descriptors + BOS + vendor-control callbacks in the usb_descriptors.cpp
//   tab (the .ino prototype generator mangles extern "C").
// - Upstream tud_cdc_line_state_cb() is DROPPED. The "connected" welcome
//   message is instead printed on the tud_cdc_connected() 0->1 edge, polled
//   in loop().
// - Vendor bulk IN is 0x84 (not upstream 0x83): the SAM3X DEVEPTCFG EPDIR
//   bit is write-once in silicon, so the bulk OUT and IN must live on
//   distinct physical endpoints.
// - LED blink via LED_BUILTIN + millis().

#include "ArduinoTinyUSB.h"

#define USB_VID 0xCafe
#define USB_PID 0x401f

enum
{
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
  BLINK_ALWAYS_ON = UINT32_MAX,
  BLINK_ALWAYS_OFF = 0
};

uint32_t blinkIntervalMs = BLINK_NOT_MOUNTED;
static bool cdcGreeted = false;

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

  ARDUINO_TINYUSB_CONSOLE.println("[WebUSB] ArduinoTinyUSB CDC + WebUSB serial");
  ARDUINO_TINYUSB_CONSOLE.println("[WebUSB] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  tud_cdc_write_flush();

  if (tud_cdc_connected())
  {
    if (!cdcGreeted)
    {
      cdcGreeted = true;
      tud_cdc_write_str("\r\nTinyUSB WebUSB device example\r\n");
    }
  }
  else
  {
    cdcGreeted = false;
  }

  ledBlinkingTask();
}
