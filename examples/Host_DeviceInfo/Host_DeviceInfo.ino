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
// Upstream: examples/host/device_info/src/main.c (tusb_config.h used for reference only).
//
// device_info — gets device descriptors of attached devices and prints them:
// VID:PID, serial, manufacturer, product strings. Descriptor snapshots are
// taken in tuh_enum_descriptor_device_cb; string fetching uses the sync
// helpers from the main loop (outside host-task callback context).
//
// Wiring:
//   Board USB host port ---> USB device (self-powered recommended: the GIGA
//     host port provides VBUS; other host ports may not)
//   ARDUINO_TINYUSB_CONSOLE (FTDI) @ 115200 for debug output

#include "ArduinoTinyUSB.h"
#include <stdarg.h>  // va_start/va_end for logf() (not pulled in on SAMD)

// English
#define LANGUAGE_ID 0x0409

enum
{
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};
static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

// Declare for buffer for usb transfer, may need to be in USB/DMA section and
// multiple of dcache line size if dcache is enabled (for some ports).
CFG_TUH_MEM_SECTION struct
{
  TUH_EPBUF_DEF(serial, 64 * sizeof(uint16_t));
  TUH_EPBUF_DEF(buf, 128 * sizeof(uint16_t));
} desc;

static tusb_desc_device_t descriptor_device[CFG_TUH_DEVICE_MAX + 1];

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
void printDevinfoTask(void);
static void printUtf16(uint16_t *temp_buf, size_t buf_len);

// One flag per possible device address — set by tuh_mount_cb and cleared by
// printDevinfoTask once the device's descriptor info has been printed.
static volatile bool need_devinfo[CFG_TUH_DEVICE_MAX + 1];

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);
  delay(1000);
  ARDUINO_TINYUSB_CONSOLE.println("[device_info] TinyUSB Device Info Example");
  ARDUINO_TINYUSB_CONSOLE.println("[device_info] Waiting for device...");

  tuh_arduino_init();
}

void loop()
{
  tuh_arduino_poll();
  printDevinfoTask();
  ledBlinkingTask();
}

extern "C"
{

void tuh_enum_descriptor_device_cb(uint8_t daddr, const tusb_desc_device_t *desc_device)
{
  if (daddr <= CFG_TUH_DEVICE_MAX)
  {
    descriptor_device[daddr] = *desc_device;
  }
}

// Invoked when device is mounted (configured). Runs in the host task — keep
// it minimal. The actual descriptor fetching/printing happens in
// printDevinfoTask() below, which runs in the main loop where the sync
// helpers are safe.
void tuh_mount_cb(uint8_t daddr)
{
  blink_interval_ms = BLINK_MOUNTED;
  if (daddr < TU_ARRAY_SIZE(need_devinfo))
  {
    need_devinfo[daddr] = true;
  }
}

// Invoked when device is unmounted (bus reset/unplugged)
void tuh_umount_cb(uint8_t daddr)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
  if (daddr < TU_ARRAY_SIZE(need_devinfo))
  {
    need_devinfo[daddr] = false;
  }
  logf("Device removed, address = %d\r\n", daddr);
}

} // extern "C"

// Print device info task — serialises descriptor fetching across all mounted
// devices using the sync helpers. Sync calls are safe here because this runs
// in the main loop (outside the host-task callback context).
static void printOneDevice(uint8_t daddr, const tusb_desc_device_t *desc_device)
{
  logf("Device %u: ID %04x:%04x SN ", daddr, desc_device->idVendor, desc_device->idProduct);

  uint8_t xfer_result = XFER_RESULT_FAILED;
  if (desc_device->iSerialNumber != 0)
  {
    xfer_result = tuh_descriptor_get_serial_string_sync(daddr, LANGUAGE_ID, desc.serial, sizeof(desc.serial));
  }
  if (XFER_RESULT_SUCCESS != xfer_result)
  {
    uint16_t *serial = (uint16_t *)(uintptr_t)desc.serial;
    serial[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * 1 + 2));
    serial[1] = '0';
    serial[2] = 0;
  }
  printUtf16((uint16_t *)(uintptr_t)desc.serial, sizeof(desc.serial) / 2);
  logf("\r\n");

  logf("Device Descriptor:\r\n");
  logf("  bLength             %u\r\n", desc_device->bLength);
  logf("  bDescriptorType     %u\r\n", desc_device->bDescriptorType);
  logf("  bcdUSB              %04x\r\n", desc_device->bcdUSB);
  logf("  bDeviceClass        %u\r\n", desc_device->bDeviceClass);
  logf("  bDeviceSubClass     %u\r\n", desc_device->bDeviceSubClass);
  logf("  bDeviceProtocol     %u\r\n", desc_device->bDeviceProtocol);
  logf("  bMaxPacketSize0     %u\r\n", desc_device->bMaxPacketSize0);
  logf("  idVendor            0x%04x\r\n", desc_device->idVendor);
  logf("  idProduct           0x%04x\r\n", desc_device->idProduct);
  logf("  bcdDevice           %04x\r\n", desc_device->bcdDevice);

  logf("  iManufacturer       %u     ", desc_device->iManufacturer);
  if (desc_device->iManufacturer != 0)
  {
    if (XFER_RESULT_SUCCESS == tuh_descriptor_get_manufacturer_string_sync(daddr, LANGUAGE_ID, desc.buf, sizeof(desc.buf)))
    {
      printUtf16((uint16_t *)(uintptr_t)desc.buf, sizeof(desc.buf) / 2);
    }
  }
  logf("\r\n");

  logf("  iProduct            %u     ", desc_device->iProduct);
  if (desc_device->iProduct != 0)
  {
    if (XFER_RESULT_SUCCESS == tuh_descriptor_get_product_string_sync(daddr, LANGUAGE_ID, desc.buf, sizeof(desc.buf)))
    {
      printUtf16((uint16_t *)(uintptr_t)desc.buf, sizeof(desc.buf) / 2);
    }
  }
  logf("\r\n");

  logf("  iSerialNumber       %u     ", desc_device->iSerialNumber);
  ARDUINO_TINYUSB_CONSOLE.print((char *)desc.serial); // serial is already UTF-8
  logf("\r\n");
  logf("  bNumConfigurations  %u\r\n", desc_device->bNumConfigurations);
}

void printDevinfoTask(void)
{
  for (uint8_t daddr = 1; daddr < TU_ARRAY_SIZE(need_devinfo); daddr++)
  {
    if (need_devinfo[daddr])
    {
      need_devinfo[daddr] = false;
      printOneDevice(daddr, &descriptor_device[daddr]);
    }
  }
}

static void convertUtf16leToUtf8(const uint16_t *utf16, size_t utf16_len, uint8_t *utf8, size_t utf8_len)
{
  // TODO: Check for runover.
  (void)utf8_len;
  // Get the UTF-16 length out of the data itself.

  for (size_t i = 0; i < utf16_len; i++)
  {
    uint16_t chr = utf16[i];
    if (chr < 0x80)
    {
      *utf8++ = chr & 0xffu;
    }
    else if (chr < 0x800)
    {
      *utf8++ = (uint8_t)(0xC0 | (chr >> 6 & 0x1F));
      *utf8++ = (uint8_t)(0x80 | (chr >> 0 & 0x3F));
    }
    else
    {
      // TODO: Verify surrogate.
      *utf8++ = (uint8_t)(0xE0 | (chr >> 12 & 0x0F));
      *utf8++ = (uint8_t)(0x80 | (chr >> 6 & 0x3F));
      *utf8++ = (uint8_t)(0x80 | (chr >> 0 & 0x3F));
    }
    // TODO: Handle UTF-16 code points that take two entries.
  }
}

// Count how many bytes a utf-16-le encoded string will take in utf-8.
static int countUtf8Bytes(const uint16_t *buf, size_t len)
{
  size_t total_bytes = 0;
  for (size_t i = 0; i < len; i++)
  {
    uint16_t chr = buf[i];
    if (chr < 0x80)
    {
      total_bytes += 1;
    }
    else if (chr < 0x800)
    {
      total_bytes += 2;
    }
    else
    {
      total_bytes += 3;
    }
    // TODO: Handle UTF-16 code points that take two entries.
  }
  return (int)total_bytes;
}

static void printUtf16(uint16_t *temp_buf, size_t buf_len)
{
  if ((temp_buf[0] & 0xff) == 0)
  {
    return; // empty
  }
  size_t utf16_len = ((temp_buf[0] & 0xff) - 2) / sizeof(uint16_t);
  size_t utf8_len = (size_t)countUtf8Bytes(temp_buf + 1, utf16_len);
  convertUtf16leToUtf8(temp_buf + 1, utf16_len, (uint8_t *)temp_buf, sizeof(uint16_t) * buf_len);
  ((uint8_t *)temp_buf)[utf8_len] = '\0';

  ARDUINO_TINYUSB_CONSOLE.print((char *)temp_buf);
}

void ledBlinkingTask(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  if (millis() - start_ms < blink_interval_ms)
  {
    return; // not enough time
  }

  start_ms += blink_interval_ms;
  digitalWrite(LED_BUILTIN, led_state ? HIGH : LOW);
  led_state = 1 - led_state; // toggle
}
