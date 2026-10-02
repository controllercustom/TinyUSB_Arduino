/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org), Nathan Conrad
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
// Device_USBTMC.ino — USB Test & Measurement Class device (device mode).
//
// Ported from TinyUSB examples/device/usbtmc. Answers *IDN? with
// "TinyUSB,ModelNumber,SerialNumber,FirmwareVer..." and implements the
// USBTMC/USB488 state machine. Requires CFG_TUD_USBTMC=1 (set in all
// ArduinoTinyUSB configs).
//
// Verify on Linux with a USBTMC host (e.g. python usbtmc + NI-VISA):
// query *IDN? and expect the TinyUSB identification string.
//
// Works on ALL boards (ArduinoTinyUSB multi-board library).
// Debug: ARDUINO_TINYUSB_CONSOLE @ ARDUINO_TINYUSB_BAUD.
//
// Adaptations:
// - Descriptors + USBTMC state machine in the usb_descriptors.cpp tab
//   (the .ino prototype generator mangles extern "C").
// - Bulk/INT endpoints use distinct physical EP numbers (OUT 0x01, IN 0x82,
//   INT 0x83): the SAM3X DEVEPTCFG EPDIR bit is write-once in silicon, so
//   the bulk OUT and IN must live on distinct physical endpoints.
// - NOTE: the Renesas GCC rejects nested designated initializers, so the
//   capabilities struct is filled field-by-field in initCapabilities().
// - LED indicator pulse via LED_BUILTIN + millis().

#include "ArduinoTinyUSB.h"

#define USB_VID 0xCafe
#define USB_PID 0x401c

enum
{
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 0,
  BLINK_SUSPENDED = 2500
};

uint32_t blinkIntervalMs = BLINK_NOT_MOUNTED;
volatile uint8_t doPulse = false;

// 488 capabilities + state machine (from upstream usbtmc_app.c).
// NOTE: Renesas GCC rejects nested designated initializers, so the struct
// is filled field-by-field in initCapabilities() instead of at definition.
#if (CFG_TUD_USBTMC_ENABLE_488)
usbtmc_response_capabilities_488_t tudUsbtmcCapabilities;
#else
usbtmc_response_capabilities_t tudUsbtmcCapabilities;
#endif

#define IEEE4882_STB_QUESTIONABLE (0x08u)
#define IEEE4882_STB_MAV          (0x10u)
#define IEEE4882_STB_SRQ          (0x40u)

const char idn[] = "TinyUSB,ModelNumber,SerialNumber,FirmwareVer123456\r\n";
volatile uint8_t status = 0;
volatile uint16_t queryState = 0;
volatile uint32_t queryDelayStart = 0;
volatile uint32_t bulkInStarted = 0;
volatile uint32_t idnQuery = 0;

uint32_t respDelay = 125u;
size_t bufferLen = 0;
size_t bufferTxIx = 0;
uint8_t buffer[225];
unsigned int msgReqLen = 0;

// Prototype for the helper owned by the usb_descriptors.cpp tab.
void initCapabilities(void);

static void usbtmcAppTaskIter(void)
{
  switch (queryState)
  {
    case 0:
      break;
    case 1:
      queryDelayStart = millis();
      queryState = 2;
      break;
    case 2:
      if ((millis() - queryDelayStart) > respDelay)
      {
        queryDelayStart = millis();
        queryState = 3;
        status |= 0x10u;
        status |= 0x40u;
      }
      break;
    case 3:
      if ((millis() - queryDelayStart) > respDelay)
      {
        queryState = 4;
      }
      break;
    case 4:
      if (bulkInStarted && (bufferTxIx == 0))
      {
        if (idnQuery)
        {
          tud_usbtmc_transmit_dev_msg_data(idn, tu_min32(sizeof(idn) - 1, msgReqLen), true, false);
          queryState = 0;
          bulkInStarted = 0;
        }
        else
        {
          bufferTxIx = tu_min32(bufferLen, msgReqLen);
          tud_usbtmc_transmit_dev_msg_data(buffer, bufferTxIx, bufferTxIx == bufferLen, false);
        }
      }
      break;
    default:
      break;
  }
}

static void ledBlinkingTask(void)
{
  static uint32_t startMs = 0;
  static bool ledState = false;

  if (blinkIntervalMs == BLINK_MOUNTED)
  {
    if (doPulse)
    {
      ledState = true;
      digitalWrite(LED_BUILTIN, HIGH);
      startMs = millis();
      doPulse = false;
    }
    else if (ledState)
    {
      if (millis() - startMs < 750)
      {
        return;
      }
      ledState = false;
      digitalWrite(LED_BUILTIN, LOW);
    }
    return;
  }

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
  initCapabilities();

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it, avoiding a mid-enumeration
  // device swap that wedges the host port.
  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[USBTMC] ArduinoTinyUSB USBTMC/USB488 device (*IDN? responder)");
  ARDUINO_TINYUSB_CONSOLE.println("[USBTMC] Device initialized. Connect USB to PC.");
}

void loop()
{
  tud_arduino_task();

  ledBlinkingTask();
  usbtmcAppTaskIter();
}
