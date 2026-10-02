// usb_descriptors.cpp — TinyUSB CDC+UAC2 descriptors (two-tab pattern).
//
// The Arduino .ino prototype generator mangles extern "C" callbacks,
// so descriptors go in a separate .cpp tab.

#include <ArduinoTinyUSB.h>

typedef const uint8_t *desc_ptr_t;

#define USB_VID 0xCafe
#define USB_PID 0x4011

#define AUDIO_SAMPLE_RATE 48000

// Defined locally only when the unified config does not provide it.
#ifndef CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE
#define CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE AUDIO_SAMPLE_RATE
#endif

// Defined locally only when the unified config does not provide it (same
// formula).
#ifndef CFG_TUD_AUDIO_EP_SZ_IN
#define CFG_TUD_AUDIO_EP_SZ_IN TUD_AUDIO_EP_SIZE(0, AUDIO_SAMPLE_RATE, 2, 1)
#endif

#define UAC2_ENTITY_CLOCK 0x04
#define UAC2_ENTITY_MIC_FEATURE_UNIT 0x02
#define UAC2_ENTITY_MIC_INPUT_TERMINAL 0x01

enum
{
  ITF_NUM_AUDIO_CONTROL = 0,
  ITF_NUM_AUDIO_STREAMING_MIC,
  ITF_NUM_CDC,
  ITF_NUM_CDC_DATA,
  ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO20_MIC_ONE_CH_DESC_LEN + TUD_CDC_DESC_LEN)

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
  TUD_AUDIO20_MIC_ONE_CH_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4,
    CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX,
    CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX * 8,
    0x81, CFG_TUD_AUDIO_EP_SZ_IN),
#if defined(ARDUINO_TINYUSB_BOARD_DUE)
  // Due SAM3X: DEVEPTCFG EPDIR is write-once in silicon, so the bulk OUT and
  // IN must live on distinct physical endpoints. Every other
  // MCU shares EP numbers across directions — upstream's shape — and some
  // cannot even address EP4+ (STM32F4 FS core: 3 usable IN/OUT; ESP32-S3 DWC2:
  // TX FIFOs for IN EP1..EP4 only), so keep 0x84 Due-only.
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, 0x83, 8, 0x02, 0x84, 64)
#else
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, 0x83, 8, 0x02, 0x82, 64)
#endif
};

static char const *string_desc_arr[] = {
  (const char[]){ 0x09, 0x04 },  // 0: English
  "Arduino-TinyUSB",              // 1: Manufacturer
  "TinyUSB CDC UAC2",             // 2: Product
  "123456",                       // 3: Serial
  "TinyUSB Audio",                // 4: Audio interface
  "TinyUSB CDC",                  // 5: CDC interface
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

// Shared app state (owned by the .ino tab).
extern uint32_t blinkIntervalMs;
extern bool mute[];
extern uint16_t volume[];
extern uint32_t sampFreq;
extern uint16_t startVal;

enum
{
  BLINK_STREAMING = 25,
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500
};

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
  blinkIntervalMs = BLINK_MOUNTED;
}

extern "C" void tud_cdc_rx_cb(uint8_t itf) {
  (void) itf;
}

extern "C" bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *request) {
  uint8_t const entity_id = TU_U16_HIGH(request->wIndex);

  if (entity_id == UAC2_ENTITY_CLOCK) {
    uint8_t const ctrl_sel = TU_U16_HIGH(request->wValue);

    if (ctrl_sel == AUDIO20_CS_CTRL_SAM_FREQ) {
      if (request->bRequest == AUDIO20_CS_REQ_CUR) {
        audio20_control_cur_4_t curf = { (int32_t) sampFreq };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &curf, sizeof(curf));
      } else if (request->bRequest == AUDIO20_CS_REQ_RANGE) {
        // Built locally: audio20_control_range_4_n_t is an anonymous struct
        // type, which cannot be shared across translation units via extern.
        audio20_control_range_4_n_t(1) range;
        range.wNumSubRanges = 1;
        range.subrange[0].bMin = 44100;
        range.subrange[0].bMax = (int32_t) sampFreq;
        range.subrange[0].bRes = 0;
        return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &range, sizeof(range));
      }
    } else if (ctrl_sel == AUDIO20_CS_CTRL_CLK_VALID &&
               request->bRequest == AUDIO20_CS_REQ_CUR) {
      audio20_control_cur_1_t cur_valid = { .bCur = 1 };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &cur_valid, sizeof(cur_valid));
    }
    return false;
  }

  if (entity_id == UAC2_ENTITY_MIC_FEATURE_UNIT) {
    uint8_t const ctrl_sel = TU_U16_HIGH(request->wValue);
    uint8_t const channel_num = TU_U16_LOW(request->wValue);

    if (ctrl_sel == AUDIO20_FU_CTRL_MUTE && request->bRequest == AUDIO20_CS_REQ_CUR) {
      audio20_control_cur_1_t mute1 = { .bCur = mute[channel_num] };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &mute1, sizeof(mute1));
    } else if (ctrl_sel == AUDIO20_FU_CTRL_VOLUME) {
      if (request->bRequest == AUDIO20_CS_REQ_RANGE) {
        audio20_control_range_2_n_t(1) range_vol;

        range_vol.wNumSubRanges = 1;
        range_vol.subrange[0].bMin = -12800;
        range_vol.subrange[0].bMax = 0;
        range_vol.subrange[0].bRes = 256;

        return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &range_vol, sizeof(range_vol));
      } else if (request->bRequest == AUDIO20_CS_REQ_CUR) {
        audio20_control_cur_2_t cur_vol = { .bCur = volume[channel_num] };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, request, &cur_vol, sizeof(cur_vol));
      }
    }
    return false;
  }

  return false;
}

extern "C" bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *request, uint8_t *buf) {
  uint8_t const entity_id = TU_U16_HIGH(request->wIndex);

  if (entity_id == UAC2_ENTITY_MIC_FEATURE_UNIT) {
    uint8_t const ctrl_sel = TU_U16_HIGH(request->wValue);
    uint8_t const channel_num = TU_U16_LOW(request->wValue);

    TU_VERIFY(request->bRequest == AUDIO20_CS_REQ_CUR);

    if (ctrl_sel == AUDIO20_FU_CTRL_MUTE) {
      TU_VERIFY(request->wLength == sizeof(audio20_control_cur_1_t));

      mute[channel_num] = ((audio20_control_cur_1_t const *) buf)->bCur;
      return true;
    } else if (ctrl_sel == AUDIO20_FU_CTRL_VOLUME) {
      TU_VERIFY(request->wLength == sizeof(audio20_control_cur_2_t));

      volume[channel_num] = ((audio20_control_cur_2_t const *) buf)->bCur;
      return true;
    }
    return false;
  }

  if (entity_id == UAC2_ENTITY_CLOCK) {
    uint8_t const ctrl_sel = TU_U16_HIGH(request->wValue);

    TU_VERIFY(request->bRequest == AUDIO20_CS_REQ_CUR);

    if (ctrl_sel == AUDIO20_CS_CTRL_SAM_FREQ) {
      TU_VERIFY(request->wLength == sizeof(audio20_control_cur_4_t));

      sampFreq = (uint32_t) ((audio20_control_cur_4_t const *) buf)->bCur;
      return true;
    }
    return false;
  }

  return false;
}

extern "C" bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *request) {
  (void) rhport;

  uint8_t const itf = TU_U16_LOW(request->wIndex);
  uint8_t const alt = TU_U16_LOW(request->wValue);

  if (ITF_NUM_AUDIO_STREAMING_MIC == itf && alt == 0) {
    blinkIntervalMs = BLINK_MOUNTED;
  }

  startVal = 0;
  return true;
}

extern "C" bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *request) {
  (void) rhport;

  uint8_t const itf = TU_U16_LOW(request->wIndex);
  uint8_t const alt = TU_U16_LOW(request->wValue);

  if (ITF_NUM_AUDIO_STREAMING_MIC == itf && alt != 0) {
    blinkIntervalMs = BLINK_STREAMING;
  }

  return true;
}
