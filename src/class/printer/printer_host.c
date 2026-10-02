/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Ha Thach (tinyusb.org)
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the TinyUSB stack.
 */

#include "tusb_option.h"

#if CFG_TUH_ENABLED && CFG_TUH_PRINTER

#include "host/usbh.h"
#include "host/usbh_pvt.h"

#include "printer_host.h"

// Level where CFG_TUSB_DEBUG must be at least for this driver is logged
#ifndef CFG_TUH_PRINTER_LOG_LEVEL
  #define CFG_TUH_PRINTER_LOG_LEVEL  CFG_TUH_LOG_LEVEL
#endif

#define TU_LOG_DRV(...)  TU_LOG(CFG_TUH_PRINTER_LOG_LEVEL, __VA_ARGS__)

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF
//--------------------------------------------------------------------+

typedef struct {
  uint8_t itf_num;
  uint8_t ep_in;       // Bulk IN endpoint (bidirectional only)
  uint8_t ep_out;      // Bulk OUT endpoint
  uint8_t max_packet_size;
  uint8_t protocol;    // 1=unidirectional, 2=bidirectional

  volatile bool configured;
  volatile bool mounted;

  // For receive polling
  CFG_TUH_MEM_ALIGN uint8_t epbuf_in[CFG_TUH_PRINTER_RX_EPSIZE];
  volatile bool ep_in_busy;
} printerh_interface_t;

static printerh_interface_t _printer_itf[CFG_TUH_DEVICE_MAX];

TU_ATTR_ALWAYS_INLINE static inline printerh_interface_t* get_itf(uint8_t daddr) {
  return &_printer_itf[daddr - 1];
}

//--------------------------------------------------------------------+
// Weak stubs
//--------------------------------------------------------------------+
TU_ATTR_WEAK void tuh_printer_mount_cb(uint8_t dev_addr) {
  (void) dev_addr;
}

TU_ATTR_WEAK void tuh_printer_umount_cb(uint8_t dev_addr) {
  (void) dev_addr;
}

TU_ATTR_WEAK void tuh_printer_rx_cb(uint8_t dev_addr, const uint8_t *data, uint16_t len) {
  (void) dev_addr;
  (void) data;
  (void) len;
}

//--------------------------------------------------------------------+
// PUBLIC API
//--------------------------------------------------------------------+

bool tuh_printer_mounted(uint8_t dev_addr) {
  printerh_interface_t* p = get_itf(dev_addr);
  return p->mounted;
}

bool tuh_printer_ready(uint8_t dev_addr) {
  printerh_interface_t* p = get_itf(dev_addr);
  return p->mounted && p->configured;
}

bool tuh_printer_configured(uint8_t dev_addr) {
  printerh_interface_t* p = get_itf(dev_addr);
  return p->configured;
}

uint8_t tuh_printer_get_status(uint8_t dev_addr, uint8_t *status) {
  printerh_interface_t* p = get_itf(dev_addr);
  TU_ASSERT(p->configured);

  tusb_control_request_t const request = {
    .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_INTERFACE,
      .type      = TUSB_REQ_TYPE_CLASS,
      .direction = TUSB_DIR_IN
    },
    .bRequest = TUSB_PRINTER_REQUEST_GET_PORT_STATUS,
    .wValue   = 0,
    .wIndex   = p->itf_num,
    .wLength  = 1
  };

  uint8_t enum_buf[1];
  tuh_xfer_t xfer = {
    .daddr       = dev_addr,
    .ep_addr     = 0,
    .setup       = &request,
    .buffer      = enum_buf,
    .complete_cb = NULL,
    .user_data   = 0
  };

  if (!tuh_control_xfer(&xfer)) {
    return 0xFF; // Error
  }

  if (status) {
    *status = enum_buf[0];
  }
  return enum_buf[0];
}

uint16_t tuh_printer_get_device_id(uint8_t dev_addr, uint8_t *buffer, uint16_t bufsize) {
  printerh_interface_t* p = get_itf(dev_addr);
  TU_ASSERT(p->configured, 0);

  // First request gets the length (2 bytes LE)
  tusb_control_request_t const request_len = {
    .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_INTERFACE,
      .type      = TUSB_REQ_TYPE_CLASS,
      .direction = TUSB_DIR_IN
    },
    .bRequest = TUSB_PRINTER_REQUEST_GET_DEVICE_ID,
    .wValue   = 0,
    .wIndex   = p->itf_num,
    .wLength  = 2
  };

  uint8_t len_buf[2];
  tuh_xfer_t xfer_len = {
    .daddr       = dev_addr,
    .ep_addr     = 0,
    .setup       = &request_len,
    .buffer      = len_buf,
    .complete_cb = NULL,
    .user_data   = 0
  };

  if (!tuh_control_xfer(&xfer_len)) {
    return 0;
  }

  uint16_t id_len = len_buf[0] | ((uint16_t)len_buf[1] << 8);
  if (id_len < 2) {
    return 0;
  }

  id_len -= 2; // Subtract the 2-byte length field itself

  if (id_len == 0 || bufsize == 0) {
    return 0;
  }

  if (id_len > bufsize) {
    id_len = bufsize;
  }

  // Now get the actual device ID
  tusb_control_request_t const request_id = {
    .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_INTERFACE,
      .type      = TUSB_REQ_TYPE_CLASS,
      .direction = TUSB_DIR_IN
    },
    .bRequest = TUSB_PRINTER_REQUEST_GET_DEVICE_ID,
    .wValue   = 0,
    .wIndex   = p->itf_num,
    .wLength  = id_len + 2  // Length includes the 2-byte length field
  };

  tuh_xfer_t xfer_id = {
    .daddr       = dev_addr,
    .ep_addr     = 0,
    .setup       = &request_id,
    .buffer      = buffer - 2, // Point before the buffer to include length
    .complete_cb = NULL,
    .user_data   = 0
  };

  if (!tuh_control_xfer(&xfer_id)) {
    return 0;
  }

  // Re-read the length from the response (in case it changed)
  uint16_t actual_len = buffer[-2] | ((uint16_t)buffer[-1] << 8);
  if (actual_len < 2) {
    return 0;
  }
  return actual_len - 2;
}

bool tuh_printer_soft_reset(uint8_t dev_addr) {
  printerh_interface_t* p = get_itf(dev_addr);
  TU_ASSERT(p->configured);

  tusb_control_request_t const request = {
    .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_INTERFACE,
      .type      = TUSB_REQ_TYPE_CLASS,
      .direction = TUSB_DIR_OUT
    },
    .bRequest = TUSB_PRINTER_REQUEST_SOFT_RESET,
    .wValue   = 0,
    .wIndex   = p->itf_num,
    .wLength  = 0
  };

  tuh_xfer_t xfer = {
    .daddr       = dev_addr,
    .ep_addr     = 0,
    .setup       = &request,
    .buffer      = NULL,
    .complete_cb = NULL,
    .user_data   = 0
  };

  return tuh_control_xfer(&xfer);
}

uint32_t tuh_printer_write(uint8_t dev_addr, const void *buffer, uint32_t bufsize, tuh_xfer_cb_t complete_cb, uintptr_t user_data) {
  printerh_interface_t* p = get_itf(dev_addr);
  TU_VERIFY(p->configured, 0);

  // Claim endpoint
  TU_VERIFY(usbh_edpt_claim(dev_addr, p->ep_out), 0);

  if (!usbh_edpt_xfer(dev_addr, p->ep_out, (void*) buffer, bufsize)) {
    (void) usbh_edpt_release(dev_addr, p->ep_out);
    return 0;
  }

  return bufsize;
}

uint32_t tuh_printer_read(uint8_t dev_addr, void *buffer, uint32_t bufsize, tuh_xfer_cb_t complete_cb, uintptr_t user_data) {
  printerh_interface_t* p = get_itf(dev_addr);
  TU_VERIFY(p->configured && p->ep_in != 0, 0);

  // Claim endpoint
  TU_VERIFY(usbh_edpt_claim(dev_addr, p->ep_in), 0);

  if (!usbh_edpt_xfer(dev_addr, p->ep_in, buffer, bufsize)) {
    (void) usbh_edpt_release(dev_addr, p->ep_in);
    return 0;
  }

  return bufsize;
}

void tuh_printer_poll(uint8_t dev_addr) {
  printerh_interface_t* p = get_itf(dev_addr);
  if (!p->mounted || p->ep_in == 0) return;
  if (p->ep_in_busy) return;

  // Start a new read transfer
  if (usbh_edpt_claim(dev_addr, p->ep_in)) {
    if (!usbh_edpt_xfer(dev_addr, p->ep_in, p->epbuf_in, sizeof(p->epbuf_in))) {
      (void) usbh_edpt_release(dev_addr, p->ep_in);
    } else {
      p->ep_in_busy = true;
    }
  }
}

//--------------------------------------------------------------------+
// TinyUSB Host Driver Interface
//--------------------------------------------------------------------+

// Note: returns bool to match the usbh_class_driver_t.init contract
// (GCC 14 treats the void/bool pointer mismatch as an error).
bool printerh_init(void) {
  tu_memclr(_printer_itf, sizeof(_printer_itf));
  return true;
}

uint16_t printerh_open(uint8_t rhport, uint8_t dev_addr, const tusb_desc_interface_t *desc_itf, uint16_t max_len) {
  (void) rhport;

  // Printer class: 0x07, subclass 0x01 (Interface), protocol 1 or 2
  TU_VERIFY(desc_itf->bInterfaceClass == 0x07 &&
            desc_itf->bInterfaceSubClass == 0x01 &&
            (desc_itf->bInterfaceProtocol == 0x01 || desc_itf->bInterfaceProtocol == 0x02), 0);

  TU_LOG_DRV("Printer open: class=0x%02x subclass=0x%02x proto=0x%02x\r\n",
             desc_itf->bInterfaceClass, desc_itf->bInterfaceSubClass, desc_itf->bInterfaceProtocol);

  // Calculate driver length
  const uint16_t drv_len =
    (uint16_t)(sizeof(tusb_desc_interface_t) + desc_itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t));
  TU_ASSERT(drv_len <= max_len, 0);

  printerh_interface_t      *p_itf = get_itf(dev_addr);
  const tusb_desc_endpoint_t *ep_desc = (const tusb_desc_endpoint_t *)tu_desc_next(desc_itf);

  // Parse endpoints
  for (uint32_t i = 0; i < desc_itf->bNumEndpoints; i++) {
    if (TUSB_DESC_ENDPOINT != ep_desc->bDescriptorType) break;

    TU_ASSERT(TUSB_XFER_BULK == ep_desc->bmAttributes.xfer, 0);
    TU_ASSERT(tuh_edpt_open(dev_addr, ep_desc), 0);

    if (TUSB_DIR_IN == tu_edpt_dir(ep_desc->bEndpointAddress)) {
      p_itf->ep_in = ep_desc->bEndpointAddress;
      TU_LOG_DRV("  EP IN  0x%02x, MPS=%u\r\n", ep_desc->bEndpointAddress, ep_desc->wMaxPacketSize);
    } else {
      p_itf->ep_out = ep_desc->bEndpointAddress;
      p_itf->max_packet_size = ep_desc->wMaxPacketSize;
      TU_LOG_DRV("  EP OUT 0x%02x, MPS=%u\r\n", ep_desc->bEndpointAddress, ep_desc->wMaxPacketSize);
    }

    ep_desc = (const tusb_desc_endpoint_t *)tu_desc_next(ep_desc);
  }

  p_itf->itf_num   = desc_itf->bInterfaceNumber;
  p_itf->protocol  = desc_itf->bInterfaceProtocol;

  TU_LOG_DRV("  Itf=%u, Proto=%s\r\n", p_itf->itf_num, p_itf->protocol == 2 ? "Bidirectional" : "Unidirectional");

  return drv_len;
}

bool printerh_set_config(uint8_t daddr, uint8_t itf_num) {
  printerh_interface_t* p = get_itf(daddr);
  TU_ASSERT(p->itf_num == itf_num);

  p->configured = true;
  p->mounted    = true;
  p->ep_in_busy = false;

  // Get device ID for identification
  uint8_t id_buf[128];
  uint16_t id_len = tuh_printer_get_device_id(daddr, id_buf, sizeof(id_buf));
  if (id_len > 0) {
    id_buf[id_len] = 0;
    TU_LOG_DRV("  Device ID: %s\r\n", id_buf);
  }

  // Get printer status
  uint8_t status = tuh_printer_get_status(daddr, NULL);
  TU_LOG_DRV("  Status: 0x%02x (error=%u, selected=%u, paper_empty=%u)\r\n",
             status,
             (status & 0x08) ? 0 : 1,
             (status & 0x10) ? 1 : 0,
             (status & 0x20) ? 1 : 0);

  tuh_printer_mount_cb(daddr);

  return true;
}

bool printerh_xfer_cb(uint8_t daddr, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
  printerh_interface_t* p = get_itf(daddr);

  if (ep_addr == p->ep_out) {
    // TX complete
    TU_LOG_DRV("Printer TX complete: %lu bytes\r\n", xferred_bytes);
  } else if (ep_addr == p->ep_in) {
    // RX complete
    p->ep_in_busy = false;
    if (result == XFER_RESULT_SUCCESS && xferred_bytes > 0) {
      tuh_printer_rx_cb(daddr, p->epbuf_in, (uint16_t) xferred_bytes);
    }
  }

  return true;
}

void printerh_close(uint8_t dev_addr) {
  printerh_interface_t* p = get_itf(dev_addr);

  TU_LOG_DRV("Printer close: dev=%u\r\n", dev_addr);

  tuh_printer_umount_cb(dev_addr);

  tu_memclr(p, sizeof(printerh_interface_t));
}

#endif
