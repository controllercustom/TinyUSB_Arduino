// usb_descriptors.cpp — TinyUSB dual-CDC descriptors (two-tab pattern).
//
// The Arduino .ino prototype generator mangles extern "C" callbacks,
// so descriptors go in a separate .cpp tab.

#include <ArduinoTinyUSB.h>

typedef const uint8_t *desc_ptr_t;

#define USB_VID 0xCafe
#define USB_PID 0x4006

enum
{
  ITF_NUM_CDC_0 = 0,
  ITF_NUM_CDC_0_DATA,
  ITF_NUM_CDC_1,
  ITF_NUM_CDC_1_DATA,
  ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_CDC_DESC_LEN)

static tusb_desc_device_t const desc_device =
{
  .bLength = sizeof(tusb_desc_device_t),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200,
  .bDeviceClass = TUSB_CLASS_MISC,
  .bDeviceSubClass = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol = MISC_PROTOCOL_IAD,
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
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_0, 4, 0x81, 16, 0x02, 0x85, 64),
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_1, 4, 0x83, 16, 0x04, 0x86, 64)
#else
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_0, 4, 0x81, 16, 0x02, 0x82, 64),
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_1, 4, 0x83, 16, 0x04, 0x84, 64)
#endif
};

static char const *string_desc_arr[] = {
  (const char[]){ 0x09, 0x04 },  // 0: English
  "Arduino-TinyUSB",              // 1: Manufacturer
  "TinyUSB Dual CDC",             // 2: Product
  "123456",                       // 3: Serial
  "TinyUSB Dual CDC",             // 4: CDC interface
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

// LED blink interval, owned by the .ino (app logic).
extern uint32_t blinkIntervalMs;

enum
{
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000
};

extern "C" void tud_mount_cb(void) {
  blinkIntervalMs = BLINK_MOUNTED;
}

extern "C" void tud_umount_cb(void) {
  blinkIntervalMs = BLINK_NOT_MOUNTED;
}

extern "C" void tud_suspend_cb(bool remote_wakeup_en) {
  (void) remote_wakeup_en;
  blinkIntervalMs = 2500;
}

extern "C" void tud_resume_cb(void) {
  blinkIntervalMs = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}
