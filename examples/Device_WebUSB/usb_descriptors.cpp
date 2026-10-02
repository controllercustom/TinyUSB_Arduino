// usb_descriptors.cpp — TinyUSB CDC+WebUSB descriptors (two-tab pattern).
//
// The Arduino .ino prototype generator mangles extern "C" callbacks,
// so descriptors go in a separate .cpp tab.

#include <ArduinoTinyUSB.h>

typedef const uint8_t *desc_ptr_t;

#define USB_VID 0xCafe
#define USB_PID 0x401f

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
  .bcdUSB = 0x0210,
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

enum
{
  VENDOR_REQUEST_WEBUSB = 1,
  VENDOR_REQUEST_MICROSOFT = 2
};

#define URL "example.tinyusb.org/webusb-serial/index.html"

// tusb_desc_webusb_url_t ends in a flexible url[] member, which C++
// cannot initialize with a designator. Mirror it with a fixed-size
// array (the trailing NUL is never sent: bLength is the exact 3+45).
static struct
{
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bScheme;
  char url[sizeof(URL)];
} const desc_url =
{
  (uint8_t) (3 + sizeof(URL) - 1), 3, 1, URL
};

#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)
#define MS_OS_20_DESC_LEN 0xB2

static uint8_t const desc_bos[] =
{
  TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 2),
  TUD_BOS_WEBUSB_DESCRIPTOR(VENDOR_REQUEST_WEBUSB, 1),
  TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT)
};

static uint8_t const desc_ms_os_20[] =
{
  U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),
  U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),
  U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_NUM_VENDOR, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),
  U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
  U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A),
  'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00,
  'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00, 0x00,
  U16_TO_U8S_LE(0x0050),
  '{', 0x00, '9', 0x00, '7', 0x00, '5', 0x00, 'F', 0x00, '4', 0x00, '4', 0x00, 'D', 0x00, '9', 0x00, '-', 0x00,
  '0', 0x00, 'D', 0x00, '0', 0x00, '8', 0x00, '-', 0x00, '4', 0x00, '3', 0x00, 'F', 0x00, 'D', 0x00, '-', 0x00,
  '8', 0x00, 'B', 0x00, '3', 0x00, 'E', 0x00, '-', 0x00, '1', 0x00, '2', 0x00, '7', 0x00, 'C', 0x00, 'A', 0x00,
  '8', 0x00, 'A', 0x00, 'F', 0x00, 'F', 0x00, 'F', 0x00, '9', 0x00, 'D', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect size");

static char const *string_desc_arr[] = {
  (const char[]){ 0x09, 0x04 },  // 0: English
  "Arduino-TinyUSB",              // 1: Manufacturer
  "TinyUSB WebUSB",               // 2: Product
  "123456",                       // 3: Serial
  "TinyUSB CDC",                  // 4: CDC interface
  "TinyUSB Vendor",               // 5: Vendor interface
};

static uint16_t _desc_str[32];

static bool web_serial_connected = false;

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

// Shared app state (owned by the .ino tab).
extern uint32_t blinkIntervalMs;

enum
{
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
  BLINK_ALWAYS_ON = UINT32_MAX
};

// BOS is board-independent core TinyUSB API, served unguarded on all boards.
extern "C" desc_ptr_t tud_descriptor_bos_cb(void) {
  return desc_bos;
}

static void echo_all(uint8_t const buf[], uint32_t count) {
  if (web_serial_connected) {
    tud_vendor_write(buf, count);
    tud_vendor_write_flush();
  }

  if (tud_cdc_connected()) {
    tud_cdc_write(buf, count);
  }
}

extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
  if (stage != CONTROL_STAGE_SETUP) {
    return true;
  }

  switch (request->bmRequestType_bit.type) {
    case TUSB_REQ_TYPE_VENDOR:
      switch (request->bRequest) {
        case VENDOR_REQUEST_WEBUSB:
          return tud_control_xfer(rhport, request, (void *) (uintptr_t) &desc_url, desc_url.bLength);

        case VENDOR_REQUEST_MICROSOFT:
          if (request->wIndex == 7) {
            uint16_t total_len;
            memcpy(&total_len, desc_ms_os_20 + 8, 2);
            return tud_control_xfer(rhport, request, (void *) (uintptr_t) desc_ms_os_20, total_len);
          } else {
            return false;
          }

        default:
          break;
      }
      break;

    case TUSB_REQ_TYPE_CLASS:
      if (request->bRequest == 0x22) {
        web_serial_connected = (request->wValue != 0);

        if (web_serial_connected) {
          digitalWrite(LED_BUILTIN, HIGH);
          blinkIntervalMs = BLINK_ALWAYS_ON;

          tud_vendor_write_str("\r\nWebUSB interface connected\r\n");
          tud_vendor_write_flush();
        } else {
          tud_vendor_write_clear();
          blinkIntervalMs = BLINK_MOUNTED;
        }

        return tud_control_status(rhport, request);
      }
      break;

    default:
      break;
  }

  return false;
}

extern "C" void tud_vendor_rx_cb(uint8_t idx, uint8_t const *buffer, uint32_t bufsize) {
  (void) idx;
  (void) buffer;
  (void) bufsize;

  while (tud_vendor_available()) {
    uint8_t buf[64];
    uint32_t const count = tud_vendor_read(buf, sizeof(buf));
    echo_all(buf, count);
  }
}

extern "C" void tud_cdc_rx_cb(uint8_t idx) {
  (void) idx;
  while (tud_cdc_available()) {
    uint8_t buf[64];
    uint32_t const count = tud_cdc_read(buf, sizeof(buf));
    echo_all(buf, count);
  }
}

extern "C" void tud_mount_cb(void) {
  blinkIntervalMs = BLINK_MOUNTED;
}

extern "C" void tud_umount_cb(void) {
  blinkIntervalMs = BLINK_NOT_MOUNTED;
}

extern "C" void tud_suspend_cb(bool remote_wakeup_en) {
  (void) remote_wakeup_en;
  blinkIntervalMs = BLINK_SUSPENDED;
}

extern "C" void tud_resume_cb(void) {
  blinkIntervalMs = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}
