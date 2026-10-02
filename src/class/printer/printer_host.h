/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Ha Thach (tinyusb.org)
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the TinyUSB stack.
 */

#ifndef TUSB_PRINTER_HOST_H_
#define TUSB_PRINTER_HOST_H_

#include "printer.h"

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// Class Driver Configuration
//--------------------------------------------------------------------+

#ifndef CFG_TUH_PRINTER
  #define CFG_TUH_PRINTER 0
#endif

#ifndef CFG_TUH_PRINTER_RX_EPSIZE
  #define CFG_TUH_PRINTER_RX_EPSIZE TUD_EPSIZE_BULK_MAX
#endif

#ifndef CFG_TUH_PRINTER_TX_EPSIZE
  #define CFG_TUH_PRINTER_TX_EPSIZE TUD_EPSIZE_BULK_MAX
#endif

//--------------------------------------------------------------------+
// Application API
//--------------------------------------------------------------------+

// Get interface info
bool tuh_printer_mounted(uint8_t dev_addr);
bool tuh_printer_ready(uint8_t dev_addr);
bool tuh_printer_configured(uint8_t dev_addr);

// Get printer status
uint8_t tuh_printer_get_status(uint8_t dev_addr, uint8_t *status);

// Get device ID
uint16_t tuh_printer_get_device_id(uint8_t dev_addr, uint8_t *buffer, uint16_t bufsize);

// Soft reset
bool tuh_printer_soft_reset(uint8_t dev_addr);

// Send data to printer (bulk OUT)
uint32_t tuh_printer_write(uint8_t dev_addr, const void *buffer, uint32_t bufsize, tuh_xfer_cb_t complete_cb, uintptr_t user_data);

// Receive data from printer (bulk IN) - bidirectional only
uint32_t tuh_printer_read(uint8_t dev_addr, void *buffer, uint32_t bufsize, tuh_xfer_cb_t complete_cb, uintptr_t user_data);

// Poll for received data
void tuh_printer_poll(uint8_t dev_addr);

//--------------------------------------------------------------------+
// Internal Callbacks (Weak)
//--------------------------------------------------------------------+

// Called when printer is mounted (ready to use)
TU_ATTR_WEAK void tuh_printer_mount_cb(uint8_t dev_addr);

// Called when printer is unmounted
TU_ATTR_WEAK void tuh_printer_umount_cb(uint8_t dev_addr);

// Called when data is received from printer (bidirectional)
TU_ATTR_WEAK void tuh_printer_rx_cb(uint8_t dev_addr, const uint8_t *data, uint16_t len);

// Internal: called by USBH
bool printerh_init(void);
uint16_t printerh_open(uint8_t rhport, uint8_t dev_addr, const tusb_desc_interface_t *desc_itf, uint16_t max_len);
bool printerh_set_config(uint8_t daddr, uint8_t itf_num);
bool printerh_xfer_cb(uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes);
void printerh_close(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif /* TUSB_PRINTER_HOST_H_ */
