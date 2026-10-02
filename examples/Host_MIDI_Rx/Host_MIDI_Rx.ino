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
// Ported to the ArduinoTinyUSB multi-board host API.
// Upstream: examples/host/midi_rx/src/main.c (tusb_config.h used for reference only).
//
// midi_rx — USB MIDI host: prints cable number + stream bytes of every packet
// received from a MIDI device. Reception is event-driven via tuh_midi_rx_cb()
// and drained with tuh_midi_stream_read() (loop until it returns 0, per the
// midi_host.h note, so the stream FIFO never blocks bulk IN).
//
// Wiring:
//   Board USB host port ---> USB MIDI device (self-powered recommended: the
//     GIGA host port provides VBUS; other host ports may not)
//   ARDUINO_TINYUSB_CONSOLE (FTDI) @ 115200 for debug output

#include "ArduinoTinyUSB.h"
#include <stdarg.h>  // va_start/va_end for logf() (not pulled in on SAMD)

static void logf(const char *fmt, ...)
{
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  ARDUINO_TINYUSB_CONSOLE.print(buf);
}

void ledBlinkingTask(void);
void midiHostRxTask(void);

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);
  delay(1000);
  ARDUINO_TINYUSB_CONSOLE.println("[midi_rx] TinyUSB Host MIDI Example");
  ARDUINO_TINYUSB_CONSOLE.println("[midi_rx] Waiting for device...");

  tuh_arduino_init();
}

void loop()
{
  tuh_arduino_poll();
  ledBlinkingTask();
  midiHostRxTask();
}

void ledBlinkingTask(void)
{
  const uint32_t interval_ms = 1000;
  static uint32_t start_ms = 0;

  static bool led_state = false;

  // Blink every interval ms
  if (millis() - start_ms < interval_ms)
  {
    return; // not enough time
  }
  start_ms += interval_ms;

  digitalWrite(LED_BUILTIN, led_state ? HIGH : LOW);
  led_state = 1 - led_state; // toggle
}

void midiHostRxTask(void)
{
  // nothing to do, we just print out received data in callback
}

extern "C"
{

// Invoked when device with MIDI interface is mounted.
void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_cb_data)
{
  logf("MIDI Interface Index = %u, Address = %u, Number of RX cables = %u, Number of TX cables = %u\r\n",
       idx, mount_cb_data->daddr, mount_cb_data->rx_cable_count, mount_cb_data->tx_cable_count);
}

// Invoked when device with MIDI interface is un-mounted
void tuh_midi_umount_cb(uint8_t idx)
{
  logf("MIDI Interface Index = %u is unmounted\r\n", idx);
}

void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred_bytes)
{
  if (xferred_bytes == 0)
  {
    return;
  }

  // Drain the stream FIFO fully: the stack note requires invoking
  // tuh_midi_stream_read() in a loop until it returns 0, otherwise leftover
  // bytes can prevent subsequent bulk IN transfers from landing.
  uint8_t buffer[48];
  uint8_t cable_num = 0;
  uint32_t bytes_read;
  do
  {
    bytes_read = tuh_midi_stream_read(idx, &cable_num, buffer, sizeof(buffer));
    if (bytes_read == 0)
    {
      break;
    }
    logf("Cable %u rx: ", cable_num);
    for (uint32_t i = 0; i < bytes_read; i++)
    {
      logf("%02X ", buffer[i]);
    }
    logf("\r\n");
  } while (bytes_read > 0);
}

void tuh_midi_tx_cb(uint8_t idx, uint32_t xferred_bytes)
{
  (void)idx;
  (void)xferred_bytes;
}

} // extern "C"
