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
// Device_HID_Composite.ino — USB composite HID device (ArduinoTinyUSB multi-board).
// Upstream: TinyUSB examples/device/hid_composite (main.c + usb_descriptors.c).
// Demo behavior: no button on the board, so a virtual button is pressed for
// 250 ms every 4 s ('a' key + mouse move + volume-down + gamepad + stylus).
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC

#include "ArduinoTinyUSB.h"

#define USB_VID 0xCafe
#define USB_PID 0x400f

enum
{
  REPORT_ID_KEYBOARD = 1,
  REPORT_ID_MOUSE,
  REPORT_ID_STYLUS_PEN,
  REPORT_ID_CONSUMER_CONTROL,
  REPORT_ID_GAMEPAD,
  REPORT_ID_COUNT
};

extern uint32_t blinkIntervalMs;

static bool lastMounted = false;

void sendHidReport(uint8_t reportId, uint32_t btn);
static void hidTask(void);
static void ledBlinkingTask(void);

// Non-static: called from tud_hid_report_complete_cb in usb_descriptors.cpp.
uint32_t demoButton(void)
{
  return ((millis() % 4000) < 250) ? 1 : 0;
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[HID_Comp] ArduinoTinyUSB composite HID kbd+mouse+stylus+consumer+gamepad (cafe:400f)");
  ARDUINO_TINYUSB_CONSOLE.println("[HID_Comp] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  bool mounted = tud_mounted();
  if (mounted != lastMounted)
  {
    lastMounted = mounted;
    ARDUINO_TINYUSB_CONSOLE.println(mounted ? "[HID_Comp] mounted" : "[HID_Comp] unmounted");
  }
  ledBlinkingTask();
  hidTask();
}

void sendHidReport(uint8_t reportId, uint32_t btn)
{
  if (!tud_hid_ready())
  {
    return;
  }

  switch (reportId)
  {
    case REPORT_ID_KEYBOARD:
    {
      static bool hasKeyboardKey = false;
      if (btn != 0u)
      {
        uint8_t keycode[6] = { 0 };
        keycode[0] = HID_KEY_A;
        tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, keycode);
        hasKeyboardKey = true;
      }
      else
      {
        if (hasKeyboardKey)
        {
          tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
        }
        hasKeyboardKey = false;
      }
      break;
    }

    case REPORT_ID_MOUSE:
    {
      int8_t const delta = 5;
      tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, delta, delta, 0, 0);
      break;
    }

    case REPORT_ID_CONSUMER_CONTROL:
    {
      static bool hasConsumerKey = false;
      if (btn != 0u)
      {
        uint16_t volumeDown = HID_USAGE_CONSUMER_VOLUME_DECREMENT;
        tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &volumeDown, 2);
        hasConsumerKey = true;
      }
      else
      {
        uint16_t emptyKey = 0;
        if (hasConsumerKey)
        {
          tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &emptyKey, 2);
        }
        hasConsumerKey = false;
      }
      break;
    }

    case REPORT_ID_GAMEPAD:
    {
      static bool hasGamepadKey = false;
      hid_gamepad_report_t report = { .x = 0, .y = 0, .z = 0, .rz = 0, .rx = 0, .ry = 0, .hat = 0, .buttons = 0 };
      if (btn != 0u)
      {
        report.hat = GAMEPAD_HAT_UP;
        report.buttons = GAMEPAD_BUTTON_A;
        tud_hid_report(REPORT_ID_GAMEPAD, &report, sizeof(report));
        hasGamepadKey = true;
      }
      else
      {
        report.hat = GAMEPAD_HAT_CENTERED;
        report.buttons = 0;
        if (hasGamepadKey)
        {
          tud_hid_report(REPORT_ID_GAMEPAD, &report, sizeof(report));
        }
        hasGamepadKey = false;
      }
      break;
    }

    case REPORT_ID_STYLUS_PEN:
    {
      static bool touchState = false;
      hid_stylus_report_t report = { .attr = 0, .x = 0, .y = 0 };
      if (btn != 0u)
      {
        report.attr = STYLUS_ATTR_TIP_SWITCH | STYLUS_ATTR_IN_RANGE;
        report.x = 100;
        report.y = 100;
        tud_hid_report(REPORT_ID_STYLUS_PEN, &report, sizeof(report));
        touchState = true;
      }
      else
      {
        report.attr = 0;
        if (touchState)
        {
          tud_hid_report(REPORT_ID_STYLUS_PEN, &report, sizeof(report));
        }
        touchState = false;
      }
      break;
    }

    default:
      break;
  }
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

  if (tud_suspended() && btn != 0u)
  {
    tud_remote_wakeup();
  }
  else
  {
    sendHidReport(REPORT_ID_KEYBOARD, btn);
  }
}

static void ledBlinkingTask(void)
{
  static uint32_t startMs = 0;
  static bool ledState = false;
  if (0u == blinkIntervalMs)
  {
    return;
  }
  if (millis() - startMs < blinkIntervalMs)
  {
    return;
  }
  startMs += blinkIntervalMs;
  digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  ledState = 1 - ledState;
}
