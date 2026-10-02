// usb_descriptors.cpp — TinyUSB USBTMC descriptors (two-tab pattern).
//
// The Arduino .ino prototype generator mangles extern "C" callbacks,
// so descriptors go in a separate .cpp tab.

#include <ArduinoTinyUSB.h>
#include <string.h>

typedef const uint8_t *desc_ptr_t;

#define USB_VID 0xCafe
#define USB_PID 0x401c

static tusb_desc_device_t const desc_device =
{
  .bLength = sizeof(tusb_desc_device_t),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200,
  .bDeviceClass = TUSB_CLASS_UNSPECIFIED,
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

enum
{
  ITF_NUM_USBTMC,
  ITF_NUM_TOTAL
};

// Bulk/INT endpoints use distinct physical EP numbers (OUT 0x01, IN 0x82,
// INT 0x83): the SAM3X DEVEPTCFG EPDIR bit is write-once in silicon, so the
// bulk OUT and IN must live on distinct physical endpoints.
#define EPNUM_USBTMC_OUT 0x01
#define EPNUM_USBTMC_IN  0x82
#define EPNUM_USBTMC_INT 0x83

#define TUD_USBTMC_DESC_MAIN(_itfnum, _epCount, _bulkLen) \
  TUD_USBTMC_IF_DESCRIPTOR(_itfnum, _epCount, 4u, TUD_USBTMC_PROTOCOL_USB488), \
  TUD_USBTMC_BULK_DESCRIPTORS(EPNUM_USBTMC_OUT, EPNUM_USBTMC_IN, _bulkLen)
#define TUD_USBTMC_DESC(_itfnum, _bulkLen) \
  TUD_USBTMC_DESC_MAIN(_itfnum, 3, _bulkLen), \
  TUD_USBTMC_INT_DESCRIPTOR(EPNUM_USBTMC_INT, 8, 16u)
#define USBTMC_DESC_LEN (TUD_USBTMC_IF_DESCRIPTOR_LEN + TUD_USBTMC_BULK_DESCRIPTORS_LEN + TUD_USBTMC_INT_DESCRIPTOR_LEN)
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + USBTMC_DESC_LEN)

static uint8_t const desc_fs_configuration[] =
{
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
  TUD_USBTMC_DESC(ITF_NUM_USBTMC, 64),
};

static char const *string_desc_arr[] = {
  (const char[]){ 0x09, 0x04 },  // 0: English
  "Arduino-TinyUSB",              // 1: Manufacturer
  "TinyUSB USBTMC",               // 2: Product
  "123456",                       // 3: Serial
  "TinyUSB USBTMC",               // 4: USBTMC interface
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
extern volatile uint8_t doPulse;
#if (CFG_TUD_USBTMC_ENABLE_488)
extern usbtmc_response_capabilities_488_t tudUsbtmcCapabilities;
#else
extern usbtmc_response_capabilities_t tudUsbtmcCapabilities;
#endif
extern const char idn[];
extern volatile uint8_t status;
extern volatile uint16_t queryState;
extern volatile uint32_t bulkInStarted;
extern volatile uint32_t idnQuery;
extern size_t bufferLen;
extern size_t bufferTxIx;
extern uint8_t buffer[225];
extern unsigned int msgReqLen;
extern uint32_t respDelay;

#define IEEE4882_STB_QUESTIONABLE (0x08u)
#define IEEE4882_STB_MAV          (0x10u)
#define IEEE4882_STB_SRQ          (0x40u)

enum
{
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 0,
  BLINK_SUSPENDED = 2500
};

void initCapabilities(void)
{
  memset(&tudUsbtmcCapabilities, 0, sizeof(tudUsbtmcCapabilities));
  tudUsbtmcCapabilities.USBTMC_status = USBTMC_STATUS_SUCCESS;
  tudUsbtmcCapabilities.bcdUSBTMC = USBTMC_VERSION;
  tudUsbtmcCapabilities.bmIntfcCapabilities.listenOnly = 0;
  tudUsbtmcCapabilities.bmIntfcCapabilities.talkOnly = 0;
  tudUsbtmcCapabilities.bmIntfcCapabilities.supportsIndicatorPulse = 1;
  tudUsbtmcCapabilities.bmDevCapabilities.canEndBulkInOnTermChar = 0;
#if (CFG_TUD_USBTMC_ENABLE_488)
  tudUsbtmcCapabilities.bcdUSB488 = USBTMC_488_VERSION;
  tudUsbtmcCapabilities.bmIntfcCapabilities488.supportsTrigger = 1;
  tudUsbtmcCapabilities.bmIntfcCapabilities488.supportsREN_GTL_LLO = 0;
  tudUsbtmcCapabilities.bmIntfcCapabilities488.is488_2 = 1;
  tudUsbtmcCapabilities.bmDevCapabilities488.SCPI = 1;
  tudUsbtmcCapabilities.bmDevCapabilities488.SR1 = 0;
  tudUsbtmcCapabilities.bmDevCapabilities488.RL1 = 0;
  tudUsbtmcCapabilities.bmDevCapabilities488.DT1 = 0;
#endif
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

extern "C" void tud_usbtmc_open_cb(uint8_t interface_id) {
  (void) interface_id;
  tud_usbtmc_start_bus_read();
}

#if (CFG_TUD_USBTMC_ENABLE_488)
extern "C" usbtmc_response_capabilities_488_t const *tud_usbtmc_get_capabilities_cb(void) {
  return &tudUsbtmcCapabilities;
}
#else
extern "C" usbtmc_response_capabilities_t const *tud_usbtmc_get_capabilities_cb(void) {
  return &tudUsbtmcCapabilities;
}
#endif

extern "C" bool tud_usbtmc_msg_trigger_cb(usbtmc_msg_generic_t *msg) {
  (void) msg;
  status |= IEEE4882_STB_SRQ;
  return true;
}

extern "C" bool tud_usbtmc_msgBulkOut_start_cb(usbtmc_msg_request_dev_dep_out const *msg_header) {
  (void) msg_header;
  bufferLen = 0;
  if (msg_header->TransferSize > sizeof(buffer)) {
    return false;
  }
  return true;
}

extern "C" bool tud_usbtmc_msg_data_cb(void *data, size_t len, bool transfer_complete) {
  if (len + bufferLen < sizeof(buffer)) {
    memcpy(&(buffer[bufferLen]), data, len);
    bufferLen += len;
  } else {
    return false;
  }
  queryState = transfer_complete;
  idnQuery = 0;

  if (transfer_complete && (len >= 4) &&
      (!strncmp("*idn?", (char *) data, 4) || !strncmp("*IDN?", (char *) data, 4))) {
    idnQuery = 1;
  }

  if (transfer_complete &&
      (!strncmp("delay ", (char *) data, 5) || !strncmp("DELAY ", (char *) data, 5))) {
    queryState = 0;
    int d = atoi((char *) data + 5);
    if (d > 10000) {
      d = 10000;
    }
    if (d < 0) {
      d = 0;
    }
    respDelay = (uint32_t) d;
  }
  tud_usbtmc_start_bus_read();
  return true;
}

extern "C" bool tud_usbtmc_msgBulkIn_complete_cb(void) {
  if ((bufferTxIx == bufferLen) || idnQuery) {
    status &= (uint8_t) ~(IEEE4882_STB_MAV);
    queryState = 0;
    bulkInStarted = 0;
    bufferTxIx = 0;
  }
  tud_usbtmc_start_bus_read();
  return true;
}

extern "C" bool tud_usbtmc_msgBulkIn_request_cb(usbtmc_msg_request_dev_dep_in const *request) {
  msgReqLen = request->TransferSize;
  if (queryState == 0 || (bufferTxIx == 0)) {
    TU_ASSERT(bulkInStarted == 0);
    bulkInStarted = 1;
  } else {
    size_t txlen = tu_min32(bufferLen - bufferTxIx, msgReqLen);
    tud_usbtmc_transmit_dev_msg_data(&buffer[bufferTxIx], txlen,
        (bufferTxIx + txlen) == bufferLen, false);
    bufferTxIx += txlen;
  }
  return true;
}

extern "C" bool tud_usbtmc_initiate_clear_cb(uint8_t *tmc_result) {
  *tmc_result = USBTMC_STATUS_SUCCESS;
  queryState = 0;
  bulkInStarted = false;
  status = 0;
  return true;
}

extern "C" bool tud_usbtmc_check_clear_cb(usbtmc_get_clear_status_rsp_t *rsp) {
  queryState = 0;
  bulkInStarted = false;
  status = 0;
  bufferTxIx = 0u;
  bufferLen = 0u;
  rsp->USBTMC_status = USBTMC_STATUS_SUCCESS;
  rsp->bmClear.BulkInFifoBytes = 0u;
  return true;
}

extern "C" bool tud_usbtmc_initiate_abort_bulk_in_cb(uint8_t *tmc_result) {
  bulkInStarted = 0;
  *tmc_result = USBTMC_STATUS_SUCCESS;
  return true;
}

extern "C" bool tud_usbtmc_check_abort_bulk_in_cb(usbtmc_check_abort_bulk_rsp_t *rsp) {
  (void) rsp;
  tud_usbtmc_start_bus_read();
  return true;
}

extern "C" bool tud_usbtmc_initiate_abort_bulk_out_cb(uint8_t *tmc_result) {
  *tmc_result = USBTMC_STATUS_SUCCESS;
  return true;
}

extern "C" bool tud_usbtmc_check_abort_bulk_out_cb(usbtmc_check_abort_bulk_rsp_t *rsp) {
  (void) rsp;
  tud_usbtmc_start_bus_read();
  return true;
}

extern "C" void tud_usbtmc_bulkIn_clearFeature_cb(void) {
}

extern "C" void tud_usbtmc_bulkOut_clearFeature_cb(void) {
  tud_usbtmc_start_bus_read();
}

extern "C" uint8_t tud_usbtmc_get_stb_cb(uint8_t *tmc_result) {
  uint8_t old_status = status;
  status = (uint8_t) (status & ~(IEEE4882_STB_SRQ));
  *tmc_result = USBTMC_STATUS_SUCCESS;
  return old_status;
}

extern "C" bool tud_usbtmc_indicator_pulse_cb(tusb_control_request_t const *msg, uint8_t *tmc_result) {
  (void) msg;
  doPulse = true;
  *tmc_result = USBTMC_STATUS_SUCCESS;
  return true;
}
