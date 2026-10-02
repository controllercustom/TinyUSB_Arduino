// usb_descriptors.cpp — TinyUSB CDC+Vendor descriptors (two-tab pattern).
//
// The Arduino .ino prototype generator mangles extern "C" callbacks,
// so descriptors go in a separate .cpp tab.
//
// The hooks below serve this exact CDC + Vendor layout explicitly.

#include <ArduinoTinyUSB.h>

typedef const uint8_t *desc_ptr_t;

#define USB_VID 0x2341
#define USB_PID 0x0058

enum
{
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_VENDOR,
  ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_VENDOR_DESC_LEN)

static tusb_desc_device_t const desc_device =
{
  .bLength = sizeof(tusb_desc_device_t),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200,
  .bDeviceClass = 0x00,
  .bDeviceSubClass = 0x00,
  .bDeviceProtocol = 0x00,
  .bMaxPacketSize0 = 64,
  .idVendor = USB_VID,
  .idProduct = USB_PID,
  .bcdDevice = 0x0100,
  .iManufacturer = 0x01,
  .iProduct = 0x02,
  .iSerialNumber = 0x03,
  .bNumConfigurations = 0x01
};

static uint8_t const desc_fs_configuration[] =
{
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
#if defined(ARDUINO_TINYUSB_BOARD_DUE)
  // Due SAM3X: DEVEPTCFG EPDIR is write-once in silicon, so each direction
  // needs its own physical endpoint. Every other MCU shares
  // EP numbers across directions — upstream's shape — and some cannot even
  // address EP4+ (STM32F4 FS core: 3 usable IN/OUT; ESP32-S3 DWC2: TX FIFOs
  // for IN EP1..EP4 only), so keep the distinct numbers Due-only.
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, 0x81, 8, 0x02, 0x85, 64),
#else
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, 0x81, 8, 0x02, 0x82, 64),
#endif
  // Vendor IN likewise: 0x84 on Due, upstream 0x83 everywhere else.
#if defined(ARDUINO_TINYUSB_BOARD_DUE)
  TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 5, 0x03, 0x84, 64)
#else
  TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 5, 0x03, 0x83, 64)
#endif
};

static char const *string_desc_arr[] = {
  (const char[]){ 0x09, 0x04 },  // 0: English
  "Arduino-TinyUSB",              // 1: Manufacturer
  "TinyUSB Vendor",               // 2: Product
  "123456",                       // 3: Serial
  "TinyUSB CDC",                  // 4: CDC interface
  "TinyUSB Vendor",               // 5: Vendor interface
};

static uint16_t _desc_str[32];

extern "C" uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_fs_configuration;
}

extern "C" uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  uint8_t chr_count;
  if (index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
    const char *str = string_desc_arr[index];
    chr_count = (uint8_t) strlen(str);
    if (chr_count > 31) chr_count = 31;
    for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
  }
  _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
  return _desc_str;
}
