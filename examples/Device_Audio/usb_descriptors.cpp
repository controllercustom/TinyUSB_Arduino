// usb_descriptors.cpp — TinyUSB UAC2 microphone descriptors (two-tab pattern).
//
// The Arduino .ino prototype generator mangles extern "C" callbacks,
// so descriptors go in a separate .cpp tab.

#include <ArduinoTinyUSB.h>

// UAC2 microphone: AC interface + AS interface = 2 audio interfaces
enum { ITF_NUM_AUDIO_AC = 0, ITF_NUM_AUDIO_AS, ITF_NUM_TOTAL };

// Endpoint address: 0x81 = EP 1 IN (isochronous)
#define EP_IN_ADDR  0x81

// EP size for 48 kHz / 16-bit / mono full-speed: ((48000+999)/1000 + 1) * 2 * 1 = 98
#define EP_IN_SZ    98

// Total configuration descriptor length
//   TUD_CONFIG_DESC_LEN = 9
//   TUD_AUDIO20_MIC_ONE_CH_DESC_LEN = 8+9+9+8+17+12+(6+(1+1)*4)+9+9+16+6+7+8 = 131
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO20_MIC_ONE_CH_DESC_LEN)

static uint8_t const desc_fs_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 200),

  // UAC2 microphone — single channel, 16-bit, 48 kHz
  TUD_AUDIO20_MIC_ONE_CH_DESCRIPTOR(
    ITF_NUM_AUDIO_AC,       // _itfnum   — first interface (AC)
    0x00,                   // _stridx   — no string for audio
    2,                      // _nBytesPerSample
    16,                     // _nBitsUsedPerSample
    EP_IN_ADDR,             // _epin
    EP_IN_SZ                // _epsize
  ),
};

static char const *string_desc_arr[] = {
  (const char[]){ 0x09, 0x04 },  // 0: English
  "Arduino-TinyUSB",              // 1: Manufacturer
  "TinyUSB Audio",                // 2: Product
  "123456",                       // 3: Serial
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
    .idProduct = 0x8006,
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
