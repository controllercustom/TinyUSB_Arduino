// usb_descriptors.cpp — TinyUSB composite HID descriptors (two-tab pattern).
//
// The Arduino .ino prototype generator mangles extern "C" callbacks,
// so descriptors go in a separate .cpp tab.

#include <ArduinoTinyUSB.h>

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

enum
{
  ITF_NUM_HID,
  ITF_NUM_TOTAL
};

#define EPNUM_HID 0x81

static uint8_t const desc_hid_report[] =
{
  TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
  TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),
  TUD_HID_REPORT_DESC_STYLUS_PEN(HID_REPORT_ID(REPORT_ID_STYLUS_PEN)),
  TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(REPORT_ID_CONSUMER_CONTROL)),
  TUD_HID_REPORT_DESC_GAMEPAD(HID_REPORT_ID(REPORT_ID_GAMEPAD))
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static uint8_t const desc_fs_configuration[] =
{
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
  TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report),
                     EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 5)
};

static char const *string_desc_arr[] = {
  (const char[]){ 0x09, 0x04 },  // 0: English
  "Arduino-TinyUSB",              // 1: Manufacturer
  "TinyUSB HID Composite",        // 2: Product
  "123456",                       // 3: Serial
};

static uint16_t _desc_str[32];

static tusb_desc_device_t const desc_device = {
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

extern "C" uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_fs_configuration;
}

extern "C" uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return desc_hid_report;
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

// ── Shared state + class callbacks (unguarded: same on all boards) ──
// sendHidReport / demoButton live in the .ino tab (app logic).

enum {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500
};

uint32_t blinkIntervalMs = BLINK_NOT_MOUNTED;

extern void sendHidReport(uint8_t reportId, uint32_t btn);
extern uint32_t demoButton(void);

extern "C" {

void tud_mount_cb(void) {
  blinkIntervalMs = BLINK_MOUNTED;
}

void tud_umount_cb(void) {
  blinkIntervalMs = BLINK_NOT_MOUNTED;
}

void tud_suspend_cb(bool remoteWakeupEn) {
  (void) remoteWakeupEn;
  blinkIntervalMs = BLINK_SUSPENDED;
}

void tud_resume_cb(void) {
  blinkIntervalMs = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len) {
  (void) instance;
  (void) len;
  uint8_t nextReportId = report[0] + 1u;
  if (nextReportId < REPORT_ID_COUNT) {
    sendHidReport(nextReportId, demoButton());
  }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t reportId, hid_report_type_t reportType,
                               uint8_t *buffer, uint16_t reqlen) {
  (void) instance;
  (void) reportId;
  (void) reportType;
  (void) buffer;
  (void) reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t reportId, hid_report_type_t reportType,
                           uint8_t const *buffer, uint16_t bufsize) {
  (void) instance;
  if (reportType == HID_REPORT_TYPE_OUTPUT) {
    if (reportId == REPORT_ID_KEYBOARD) {
      if (bufsize < 1) {
        return;
      }
      uint8_t const kbdLeds = buffer[0];
      if ((kbdLeds & KEYBOARD_LED_CAPSLOCK) != 0u) {
        blinkIntervalMs = 0;
        digitalWrite(LED_BUILTIN, HIGH);
      } else {
        digitalWrite(LED_BUILTIN, LOW);
        blinkIntervalMs = BLINK_MOUNTED;
      }
    }
  }
}

} // extern "C"
