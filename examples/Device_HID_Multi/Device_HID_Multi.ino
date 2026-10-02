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
// Device_HID_Multi.ino — USB keyboard + mouse on separate HID interfaces (ArduinoTinyUSB multi-board).
// Upstream: TinyUSB examples/device/hid_multiple_interface (main.c + usb_descriptors.c).
// Demo behavior: no button on the board, so a virtual button is pressed for
// 250 ms every 4 s ('a' key + mouse move down-right).
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC

#include "ArduinoTinyUSB.h"

#define USB_VID 0xCafe
#define USB_PID 0x4013

enum
{
  ITF_KEYBOARD = 0,
  ITF_MOUSE = 1
};

extern uint32_t blinkIntervalMs;

static bool lastMounted = false;

static uint32_t demoButton(void)
{
  return ((millis() % 4000) < 250) ? 1 : 0;
}

static void hidTask(void);
static void ledBlinkingTask(void);

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[HID_Multi] ArduinoTinyUSB keyboard + mouse interfaces (cafe:4013)");
  ARDUINO_TINYUSB_CONSOLE.println("[HID_Multi] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  bool mounted = tud_mounted();
  if (mounted != lastMounted)
  {
    lastMounted = mounted;
    ARDUINO_TINYUSB_CONSOLE.println(mounted ? "[HID_Multi] mounted" : "[HID_Multi] unmounted");
  }
  ledBlinkingTask();
  hidTask();
}

static void hidTask(void)
{
  const uint32_t intervalMs = 10;
  static uint32_t startMs = 0;
  if (millis() - startMs < intervalMs)
  {
    return;
  }
  startMs += intervalMs;

  uint32_t const btn = demoButton();

  if (tud_suspended() && btn)
  {
    tud_remote_wakeup();
  }

  if (tud_hid_n_ready(ITF_KEYBOARD))
  {
    static bool hasKey = false;
    if (btn)
    {
      uint8_t keycode[6] = { 0 };
      keycode[0] = HID_KEY_A;
      tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, 0, keycode);
      hasKey = true;
    }
    else
    {
      if (hasKey)
      {
        tud_hid_n_keyboard_report(ITF_KEYBOARD, 0, 0, NULL);
      }
      hasKey = false;
    }
  }

  // Second HID instance exists only where the config enables it
  // (CFG_TUD_HID > 1).
#if CFG_TUD_HID > 1
  if (tud_hid_n_ready(ITF_MOUSE))
  {
    if (btn)
    {
      int8_t const delta = 5;
      tud_hid_n_mouse_report(ITF_MOUSE, 0, 0x00, delta, delta, 0, 0);
    }
  }
#endif
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
  ledState = 1 - ledState;
}
