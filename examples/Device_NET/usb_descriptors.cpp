// usb_descriptors.cpp — TinyUSB CDC-NCM descriptors (two-tab pattern).
//
// The Arduino .ino prototype generator mangles extern "C" callbacks,
// so descriptors go in a separate .cpp tab.

#include <ArduinoTinyUSB.h>

enum { ITF_NUM_CDC_NCM = 0, ITF_NUM_CDC_NCM_DATA, ITF_NUM_TOTAL };

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_NCM_DESC_LEN)

static uint8_t const desc_fs_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
  // CDC-NCM: notification EP 0x81 (INTR IN), data OUT 0x02, data IN 0x83.
  // NOTE: data EPs use 64-byte packets (FS bulk max) and distinct physical
  // endpoint numbers per direction — SAM3X EPDIR is write-once per physical
  // endpoint, so EP2 cannot serve both IN and OUT. The
  // macro emits addresses verbatim: _epout must be an OUT address (0x02),
  // _epin an IN address (0x83); the old 0x82/0x02 pair was swapped and the
  // 512-byte size is HS-only (FS bulk: 8/16/32/64), both tripping
  // tu_edpt_validate → netd_open → set_config ASSERT on device.
  TUD_CDC_NCM_DESCRIPTOR(ITF_NUM_CDC_NCM, 4, 5, 0x81, 8, 0x02, 0x83, 64, 1514, 1, 0),
};

static char const *string_desc_arr[] = {
  (const char[]){ 0x09, 0x04 },  // 0: English
  "Arduino-TinyUSB",              // 1: Manufacturer
  "TinyUSB NET",                  // 2: Product
  "123456",                       // 3: Serial
  "TinyUSB CDC-NCM",              // 4: CDC-NCM control interface
  "0202846A9600",                 // 5: MAC address string
};

static uint16_t _desc_str[32];

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x2341,
    .idProduct = 0x8007,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

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
