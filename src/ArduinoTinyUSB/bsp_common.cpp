/*
 * Arduino-TinyUSB Common BSP Implementation
 *
 * Shared code across all boards: debug printf, time source, weak callbacks.
 */

#include "bsp_common.h"

// Always the vendored TinyUSB from src/.
#ifdef __cplusplus
extern "C" {
#endif
  #include "tusb.h"
#ifdef __cplusplus
}
#endif

// ─── Debug Printf ───
extern "C" int arduino_debug_printf(char const *format, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, format);
  int n = vsnprintf(buf, sizeof(buf), format, ap);
  va_end(ap);
#if defined(ARDUINO_TINYUSB_BOARD_ESP32)
  // ESP32: the USB ISR runs TinyUSB callbacks (e.g. audiod_tx_xfer_isr at
  // 1 kHz for ISO streaming). Blocking on the UART TX FIFO here starves the
  // interrupt watchdog (observed on hardware: Guru Meditation, Interrupt wdt
  // timeout on CPU1 in uart_hal_write_txfifo via usbd_edpt_xfer_fifo). Drop
  // ISR-context logs; loop-context logging is unaffected.
  if (xPortInIsrContext() != pdFALSE) return n;
#endif
  if (n > 0) {
    ARDUINO_TINYUSB_CONSOLE.write(buf, (n < (int)sizeof(buf)) ? n : sizeof(buf));
  }
  return n;
}

// ─── Time Source ───
extern "C" uint32_t tusb_time_millis_api(void) {
  return (uint32_t)millis();
}

// ─── Weak Default Device Callbacks ───
// HID device callbacks (hid_report_type_t only exists when CFG_TUD_HID > 0)
TU_ATTR_WEAK uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return NULL;
}

#if CFG_TUD_HID
TU_ATTR_WEAK uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                             hid_report_type_t report_type,
                                             uint8_t *buffer, uint16_t reqlen) {
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
  return 0;
}

TU_ATTR_WEAK void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                                         hid_report_type_t report_type,
                                         uint8_t const *buffer, uint16_t bufsize) {
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}
#endif // CFG_TUD_HID

// MSC device callbacks
TU_ATTR_WEAK void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                                     uint8_t product_id[16], uint8_t product_rev[4]) {
  (void)lun;
  const char vid[] = "Arduino";
  const char pid[] = "TinyUSB Disk";
  const char rev[] = "1.0";
  memcpy(vendor_id, vid, strlen(vid));
  memcpy(product_id, pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));
}

TU_ATTR_WEAK bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  (void)lun;
  return false;
}

TU_ATTR_WEAK void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                                      uint16_t *block_size) {
  (void)lun;
  *block_count = 0;
  *block_size  = 512;
}

TU_ATTR_WEAK int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                                        void *buffer, uint32_t bufsize) {
  (void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
  return -1;
}

TU_ATTR_WEAK int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                                         uint8_t *buffer, uint32_t bufsize) {
  (void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
  return -1;
}

TU_ATTR_WEAK int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                                      void *buffer, uint16_t bufsize) {
  (void)lun; (void)scsi_cmd; (void)buffer; (void)bufsize;
  return -1;
}

// USBTMC device callbacks — the class driver provides no weak defaults,
// so without these every sketch would fail to link with CFG_TUD_USBTMC=1.
// (Sketches using USBTMC override them strongly.)

#if CFG_TUD_USBTMC
#if CFG_TUD_USBTMC_ENABLE_488
static usbtmc_response_capabilities_488_t _usbtmc_caps;
#else
static usbtmc_response_capabilities_t _usbtmc_caps;
#endif

TU_ATTR_WEAK
#if CFG_TUD_USBTMC_ENABLE_488
usbtmc_response_capabilities_488_t const *
#else
usbtmc_response_capabilities_t const *
#endif
tud_usbtmc_get_capabilities_cb(void)
{
  memset(&_usbtmc_caps, 0, sizeof(_usbtmc_caps));
  _usbtmc_caps.USBTMC_status = USBTMC_STATUS_SUCCESS;
  _usbtmc_caps.bcdUSBTMC = USBTMC_VERSION;
  return &_usbtmc_caps;
}

TU_ATTR_WEAK void tud_usbtmc_open_cb(uint8_t interface_id)
{ (void)interface_id; }

TU_ATTR_WEAK bool tud_usbtmc_msgBulkOut_start_cb(usbtmc_msg_request_dev_dep_out const *msgHeader)
{ (void)msgHeader; return false; }

TU_ATTR_WEAK bool tud_usbtmc_msg_data_cb(void *data, size_t len, bool transfer_complete)
{ (void)data; (void)len; (void)transfer_complete; return false; }

TU_ATTR_WEAK bool tud_usbtmc_msgBulkIn_request_cb(usbtmc_msg_request_dev_dep_in const *request)
{ (void)request; return true; }

TU_ATTR_WEAK bool tud_usbtmc_msgBulkIn_complete_cb(void)
{ return true; }

TU_ATTR_WEAK void tud_usbtmc_bulkOut_clearFeature_cb(void)
{ }

TU_ATTR_WEAK void tud_usbtmc_bulkIn_clearFeature_cb(void)
{ }

TU_ATTR_WEAK bool tud_usbtmc_initiate_clear_cb(uint8_t *tmcResult)
{ *tmcResult = USBTMC_STATUS_SUCCESS; return true; }

TU_ATTR_WEAK bool tud_usbtmc_check_clear_cb(usbtmc_get_clear_status_rsp_t *rsp)
{ rsp->USBTMC_status = USBTMC_STATUS_SUCCESS; rsp->bmClear.BulkInFifoBytes = 0u; return true; }

TU_ATTR_WEAK bool tud_usbtmc_initiate_abort_bulk_in_cb(uint8_t *tmcResult)
{ *tmcResult = USBTMC_STATUS_SUCCESS; return true; }

TU_ATTR_WEAK bool tud_usbtmc_initiate_abort_bulk_out_cb(uint8_t *tmcResult)
{ *tmcResult = USBTMC_STATUS_SUCCESS; return true; }

TU_ATTR_WEAK bool tud_usbtmc_check_abort_bulk_in_cb(usbtmc_check_abort_bulk_rsp_t *rsp)
{ (void)rsp; return true; }

TU_ATTR_WEAK bool tud_usbtmc_check_abort_bulk_out_cb(usbtmc_check_abort_bulk_rsp_t *rsp)
{ (void)rsp; return true; }

TU_ATTR_WEAK uint8_t tud_usbtmc_get_stb_cb(uint8_t *tmcResult)
{ *tmcResult = USBTMC_STATUS_SUCCESS; return 0; }
#endif // CFG_TUD_USBTMC

TU_ATTR_WEAK bool tud_usbtmc_msg_trigger_cb(uint8_t rhport, void *data, uint16_t len) {
  (void)rhport; (void)data; (void)len;
  return false;
}

// DFU Runtime device callback
TU_ATTR_WEAK void tud_dfu_runtime_reboot_to_dfu_cb(void) {
}

// Network NCM device callbacks
TU_ATTR_WEAK bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  (void)src; (void)size;
  return true;
}

// Device task pump (see ArduinoTinyUSB.h): non-blocking poll on ESP32
// (FreeRTOS queue receive would block loop() forever when USB idles),
// plain tud_task() everywhere else.
extern "C" void tud_arduino_task(void) {
#if CFG_TUD_ENABLED
#if defined(ARDUINO_TINYUSB_BOARD_ESP32)
  tud_task_ext(0, false);
#else
  tud_task();
#endif
#endif
}

TU_ATTR_WEAK uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  (void)dst; (void)ref; (void)arg;
  return 0;
}
