/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 ChrisDeadman
 * SPDX-FileCopyrightText: Copyright (c) 2024 Ha Thach (tinyusb.org)
 * SPDX-License-Identifier: MIT
 *
 * This file is part of the TinyUSB stack.
 */

#include "tusb_option.h"

#if CFG_TUH_ENABLED && !(defined(CFG_TUH_MAX3421) && CFG_TUH_MAX3421) && \
    TU_CHECK_MCU(OPT_MCU_SAMD11, OPT_MCU_SAMD21, OPT_MCU_SAML2X, OPT_MCU_SAMD51, OPT_MCU_SAME5X)

#include "host/hcd.h"
#include "sam.h"

/*------------------------------------------------------------------*/
/* MACRO TYPEDEF CONSTANT ENUM
 *------------------------------------------------------------------*/
#define USB_HOST_PTYPE_DIS         0x0
#define USB_HOST_PTYPE_CTRL        0x1
#define USB_HOST_PTYPE_ISO         0x2
#define USB_HOST_PTYPE_BULK        0x3
#define USB_HOST_PTYPE_INT         0x4
#define USB_HOST_PTYPE_EXT         0x5

#define USB_HOST_PCFG_PTOKEN_SETUP 0x0
#define USB_HOST_PCFG_PTOKEN_IN    0x1
#define USB_HOST_PCFG_PTOKEN_OUT   0x2

#define USB_PCKSIZE_ENUM(size) \
  ((size) >= 1024      ? 7     \
      : (size) >= 1023 ? 7     \
      : (size) > 256   ? 6     \
      : (size) > 128   ? 5     \
      : (size) > 64    ? 4     \
      : (size) > 32    ? 3     \
      : (size) > 16    ? 2     \
      : (size) > 8     ? 1     \
                       : 0)

// Uncomment to use fake frame number.
// Low-Speed devices stall FNUM during enumeration :/
// #define HCD_SAMD_FAKE_FNUM

typedef struct {
  uint8_t dev_addr;
  uint8_t ep_addr;
  uint16_t max_packet_size;
  uint16_t xfer_length;
  uint16_t xfer_remaining;
  uint8_t bmRcvToggle;  // SAMD21 quirk: software-managed data toggle for IN
  uint8_t bmSndToggle;  // SAMD21 quirk: software-managed data toggle for OUT
} usb_pipe_status_t;

CFG_TUH_MEM_SECTION CFG_TUH_MEM_ALIGN static volatile UsbHostDescriptor usb_pipe_table[USB_PIPE_NUM];

CFG_TUH_MEM_SECTION CFG_TUH_MEM_ALIGN static volatile usb_pipe_status_t usb_pipe_status_table[USB_PIPE_NUM];

CFG_TUH_MEM_SECTION CFG_TUH_MEM_ALIGN static volatile uint32_t fake_fnum;

// Does this MCU need the software-managed data-toggle workaround?
// SAMD51 DOES, and this is measured, not assumed: with the workaround disabled
// the very first IN data stage after a SETUP token comes out as DATA0 and the
// device answers with a data-toggle error, every attempt —
//   PIPE0 TRFAIL ep=80 PINTFLAG=08 STATUS_PIPE=0000 PSTATUS=10
//   USB_HOST_STATUS_PIPE_DTGLER
// The SAMD51 host engine resets DTGL to DATA0 on a SETUP token, and the spec
// then requires DATA1 for the following data stage, so the port has to program
// DTGL=1 itself (bmRcvToggle/bmSndToggle = 1 in hcd_setup_send).
// Verified 2026-09-26 on the GCM4 against an RP2350 device, SAMD_HOST_SW_TOGGLE=0.
#if 1
  #define SAMD_HOST_SW_TOGGLE 1
#else
  #define SAMD_HOST_SW_TOGGLE 0
#endif

// Wall-clock bound for one bus-reset attempt. A USB bus reset is >= 10 ms
// (USB 2.0 spec 7.1.7.5) and measured on this silicon the RST flag lands
// 10-20 ms after BUSRESET is written.
#define SAMD_BUSRESET_WAIT_MS 30

// Bus-reset completion latch. Waiting on USB->HOST.INTFLAG.bit.RST alone does
// not work: with CFG_TUSB_OS = OPT_OS_NONE the event-queue spinlock IS the
// interrupt enable (usbh.c:163, `OSAL_SPINLOCK_DEF(_usbh_spin, usbh_int_set)`),
// so any other code path that takes that lock calls hcd_int_enable() behind
// hcd_port_reset()'s back, and hcd_int_handler() clears the RST flag when it
// sees it. The wait would then ALWAYS time out, and the recovery path
// (re-issue BUSRESET) returned while SE0 was still asserted — the GCM4 host
// (120 MHz SAMD51) then failed every enumeration with
// `hcd_port_reset_end: RST timeout` -> connect-status reads 0 two ms later
// -> "device unplugged while delaying" -> `success = 0` in a permanent
// attach/remove loop. Latch completion in software and wait on that OR the
// hardware flag.
static volatile bool _samd_busreset_done = false;

static uint8_t samd_configure_pipe(uint8_t dev_addr, uint8_t ep_addr, bool is_setup)
{
  uint8_t pipe;
  uint8_t token;
  volatile usb_pipe_status_t* pipe_status;
  bool same_addr = false;
  bool same_ep_addr = false;

  // Evaluate pipe token. SETUP token is ONLY for the 8-byte setup stage
  // (sent via hcd_setup_send). EP0 OUT data/status stages must use OUT:
  // sending a status stage as SETUP + zero-length DATA0 is a spec violation
  // and the host's own next SETUP then goes unanswered — enumeration dies
  // right after SET_ADDRESS on ANY device, not just strict ones. A/B on
  // SAMD21 (zero) <-> RP2040 (pico): enumerates + mounts CDC with the fix,
  // TIMES OUT at the first Get Device Descriptor at the new address
  // without it (2026-09-27). The SAMD21 *device* side is not involved:
  // the same zero as a USB device enumerates cleanly behind an RP2040
  // host, so this is purely the host token choice.
  token = (tu_edpt_dir(ep_addr) == TUSB_DIR_IN) ? USB_HOST_PCFG_PTOKEN_IN
        : (is_setup && tu_edpt_number(ep_addr) == 0) ? USB_HOST_PCFG_PTOKEN_SETUP
                                                    : USB_HOST_PCFG_PTOKEN_OUT;

  TU_LOG3("samd_configure_pipe(token=%02X, dev_addr=%02X, ep_addr=%02X)=", token, dev_addr, ep_addr);

  // find already allocated pipe
  for (pipe = 0; pipe < USB_PIPE_NUM; pipe++) {
    pipe_status = &usb_pipe_status_table[pipe];
    same_addr = (pipe_status->dev_addr == dev_addr);
    same_ep_addr = (tu_edpt_number(pipe_status->ep_addr) == tu_edpt_number(ep_addr));
    if (same_ep_addr && (same_addr || (tu_edpt_number(ep_addr) == 0))) {
      break;
    }
  }

  // allocate from pool of free pipes
  if (pipe >= USB_PIPE_NUM) {
    for (pipe = 0; pipe < USB_PIPE_NUM; pipe++) {
      pipe_status = &usb_pipe_status_table[pipe];
      // found a free pipe
      if (pipe_status->dev_addr >= UINT8_MAX) {
        break;
      }
    }
  }

  // no pipe available :(
  if (pipe >= USB_PIPE_NUM) {
    TU_LOG3("ERR_NO_PIPE\r\n");
    return pipe;
  }
  TU_LOG3("%d\r\n", pipe);

  // no transfer should be in progress
  TU_ASSERT(((USB->HOST.HostPipe[pipe].PCFG.bit.PTYPE == USB_HOST_PTYPE_DIS) ||
                USB->HOST.HostPipe[pipe].PSTATUS.bit.PFREEZE == 1),
      USB_PIPE_NUM);

  // update addr and ep_addr
  pipe_status->dev_addr = dev_addr;
  pipe_status->ep_addr = ep_addr;
  usb_pipe_table[pipe].HostDescBank[0].CTRL_PIPE.bit.PDADDR = dev_addr;
  usb_pipe_table[pipe].HostDescBank[0].CTRL_PIPE.bit.PEPNUM = tu_edpt_number(ep_addr);

  // SAMD21 quirk: PTOKEN can only be modified when PTYPE is DIS.
  // Disable pipe type, change token, then re-enable as control.
  USB->HOST.HostPipe[pipe].PCFG.reg &= ~USB_HOST_PCFG_PTYPE_Msk;
  USB->HOST.HostPipe[pipe].PCFG.bit.PTOKEN = token;
  USB->HOST.HostPipe[pipe].PCFG.bit.PTYPE = USB_HOST_PTYPE_CTRL;
  USB->HOST.HostPipe[pipe].PINTENCLR.reg = USB_HOST_PINTENCLR_MASK;
  if (token == USB_HOST_PCFG_PTOKEN_SETUP) {
    USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_DTGL;
    USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_BK0RDY;
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_TXSTP;
    // A SETUP token is only half the EP0 stage: the device's data stage comes
    // back on the SAME pipe as an IN or OUT packet and completes via TRCPT0
    // (or TRCPT1). Without enabling TRCPT here the host is blind to it — the
    // setup stage times out with the ISR never having run for that pipe.
    // Observed on hardware on the GCM4 (SAMD51): a Pico 2 device logged the
    // SETUP, queued the data stage and completed it, while the host logged
    // zero pipe events and gave up with "on EP 00 with 0 bytes: TIMEOUT". The SAMD21
    // gets away with it because its hardware also raises TRFAIL/ERRORFLOW on
    // that packet, which IS enabled and wakes the ISR; the SAMD51 raises only
    // TRCPT. Harmless on the SAMD21 — TRCPT is simply a flag the ISR already
    // handles, and it can only fire when there is data to receive.
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_TRCPT_Msk;
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_STALL;
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_TRFAIL;
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_PERR;
  } else if (token == USB_HOST_PCFG_PTOKEN_IN) {
    USB->HOST.HostPipe[pipe].PSTATUSSET.reg = USB_HOST_PSTATUSSET_BK0RDY;
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_TRCPT_Msk;
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_STALL;
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_TRFAIL;
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_PERR;
  } else {
    USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_BK0RDY;
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_TRCPT_Msk;
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_STALL;
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_TRFAIL;
    USB->HOST.HostPipe[pipe].PINTENSET.reg = USB_HOST_PINTENSET_PERR;
  }

  return pipe;
}

static void samd_free_pipe(uint8_t pipe)
{
  volatile usb_pipe_status_t* pipe_status = &usb_pipe_status_table[pipe];
  pipe_status->dev_addr = UINT8_MAX;
  pipe_status->ep_addr = UINT8_MAX;
  pipe_status->max_packet_size = 0;
  pipe_status->xfer_length = 0;
  pipe_status->xfer_remaining = 0;
  pipe_status->bmRcvToggle = 0;
  pipe_status->bmSndToggle = 0;

  USB->HOST.HostPipe[pipe].PSTATUSSET.reg = USB_HOST_PSTATUSSET_PFREEZE;
  USB->HOST.HostPipe[pipe].PCFG.reg &= ~USB_HOST_PCFG_PTYPE_Msk;
  USB->HOST.HostPipe[pipe].PINTENCLR.reg = USB_HOST_PINTENCLR_MASK;
  memset((uint8_t*)(uintptr_t) &usb_pipe_table[pipe], 0, sizeof(usb_pipe_table[pipe]));
}

static void samd_free_all_pipes(void)
{
  for (uint8_t pipe = 0; pipe < USB_PIPE_NUM; pipe++) {
    samd_free_pipe(pipe);
  }
}

static bool samd_on_xfer(uint8_t pipe, xfer_result_t xfer_result)
{
  uint16_t xfer_delta;
  bool xfer_complete;
  volatile usb_pipe_status_t* pipe_status = &usb_pipe_status_table[pipe];

  // freeze the pipe
  USB->HOST.HostPipe[pipe].PSTATUSSET.reg = USB_HOST_PSTATUSSET_PFREEZE;

  // get number of transferred bytes
  if (xfer_result == XFER_RESULT_SUCCESS) {
    xfer_delta = usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.BYTE_COUNT;
    // SAMD21 quirk: hardware auto-toggle is broken — toggle software state manually.
    // ISO pipes carry no toggle (see hcd_edpt_xfer), so leave the state alone.
    bool const is_iso =
        (USB->HOST.HostPipe[pipe].PCFG.bit.PTYPE == USB_HOST_PTYPE_ISO);
    if (!is_iso && SAMD_HOST_SW_TOGGLE) {
      if (tu_edpt_dir(pipe_status->ep_addr) == TUSB_DIR_IN) {
        pipe_status->bmRcvToggle ^= 1;
      } else {
        pipe_status->bmSndToggle ^= 1;
      }
    }
  } else {
    xfer_delta = 0;
  }

  TU_LOG3("samd_on_xfer(%d, result=%d, xdelta=%d, rem=%d)\r\n", xfer_result, pipe, xfer_delta, pipe_status->xfer_remaining);

  // update pipe status
  if (xfer_delta > pipe_status->xfer_remaining) {
    xfer_delta = pipe_status->xfer_remaining;
  }
  pipe_status->xfer_remaining -= xfer_delta;
  pipe_status->xfer_length += xfer_delta;

  // last packet handling
  if (xfer_delta < pipe_status->max_packet_size) {
    pipe_status->xfer_remaining = 0;
  }

  // transfer complete
  xfer_complete = (xfer_result != XFER_RESULT_SUCCESS) || (pipe_status->xfer_remaining == 0);
  if (xfer_complete) {
    return true;
  }

  // continue receiving
  if (tu_edpt_dir(pipe_status->ep_addr) == TUSB_DIR_IN) {
    usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.BYTE_COUNT = 0;
    USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_BK0RDY;
  }
  // continue sending
  else {
    usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.BYTE_COUNT =
        (pipe_status->xfer_remaining < pipe_status->max_packet_size) ? pipe_status->xfer_remaining
                                                                     : pipe_status->max_packet_size;
    USB->HOST.HostPipe[pipe].PSTATUSSET.reg = USB_HOST_PSTATUSSET_BK0RDY;
  }

  // advance packet buffer
  usb_pipe_table[pipe].HostDescBank[0].ADDR.reg += xfer_delta;

  // start next transfer
  USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_PFREEZE;

  return false;
}

//--------------------------------------------------------------------+
// Controller API
//--------------------------------------------------------------------+

// Interrupt Handler
void hcd_int_handler(uint8_t rhport, bool in_isr)
{
  (void) rhport;

  uint16_t int_flags;
  uint8_t pint_flags;
  xfer_result_t xfer_result;
  volatile usb_pipe_status_t* pipe_status;

  //
  // Check INTFLAG
  //
  int_flags = USB->HOST.INTFLAG.reg;
  if (int_flags & USB_HOST_INTFLAG_HSOF) {
    USB->HOST.INTFLAG.reg = USB_HOST_INTFLAG_HSOF;
  }
  if (int_flags & USB_HOST_INTFLAG_RST) {
    TU_LOG2("USB_HOST_INTFLAG_RST\r\n");
    USB->HOST.INTFLAG.reg = USB_HOST_INTFLAG_RST;
    _samd_busreset_done = true; // consumed here; latch it for hcd_port_reset_end()
  }
  if (int_flags & USB_HOST_INTFLAG_WAKEUP) {
    TU_LOG3("USB_HOST_INTFLAG_WAKEUP\r\n");
    USB->HOST.INTFLAG.reg = USB_HOST_INTFLAG_WAKEUP;
  }
  if (int_flags & USB_HOST_INTFLAG_DNRSM) {
    TU_LOG3("USB_HOST_INTFLAG_DNRSM\r\n");
    USB->HOST.INTFLAG.reg = USB_HOST_INTFLAG_DNRSM;
  }
  if (int_flags & USB_HOST_INTFLAG_UPRSM) {
    TU_LOG3("USB_HOST_INTFLAG_UPRSM\r\n");
    USB->HOST.INTFLAG.reg = USB_HOST_INTFLAG_UPRSM;
  }
  if (int_flags & USB_HOST_INTFLAG_RAMACER) {
    TU_LOG1("USB_HOST_INTFLAG_RAMACER\r\n");
    USB->HOST.INTFLAG.reg = USB_HOST_INTFLAG_RAMACER;
  }
  if (int_flags & USB_HOST_INTFLAG_DCONN) {
    USB->HOST.INTFLAG.reg = USB_HOST_INTFLAG_DCONN;
    hcd_event_device_attach(rhport, in_isr);
  }
  if (int_flags & USB_HOST_INTFLAG_DDISC) {
    USB->HOST.INTFLAG.reg = USB_HOST_INTFLAG_DDISC;
    hcd_event_device_remove(rhport, in_isr);
  }

  // handle pipe interrupts
  for (uint8_t pipe = 0; pipe < USB_PIPE_NUM; pipe++) {
    // get pipe handle
    pipe_status = &usb_pipe_status_table[pipe];
    if (pipe_status->dev_addr >= UINT8_MAX) {
      continue;
    }

    //
    // Check PINTFLAG
    //
    pint_flags = USB->HOST.HostPipe[pipe].PINTFLAG.reg;
    xfer_result = XFER_RESULT_INVALID;
    if (pint_flags & USB_HOST_PINTFLAG_TRCPT0) {
      USB->HOST.HostPipe[pipe].PINTFLAG.reg = USB_HOST_PINTFLAG_TRCPT0;
      TU_LOG2("PIPE%d TRCPT0 ep=%02X bytecount=%u STATUS_PIPE=%04X PSTATUS=%02X\r\n",
              pipe, pipe_status->ep_addr,
              (unsigned) usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.BYTE_COUNT,
              (unsigned) usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg,
              (unsigned) USB->HOST.HostPipe[pipe].PSTATUS.reg);
      xfer_result = XFER_RESULT_SUCCESS;
    }
    if (pint_flags & USB_HOST_PINTFLAG_TRCPT1) {
      USB->HOST.HostPipe[pipe].PINTFLAG.reg = USB_HOST_PINTFLAG_TRCPT1;
      TU_LOG2("PIPE%d TRCPT1 ep=%02X bytecount=%u STATUS_PIPE=%04X PSTATUS=%02X\r\n",
              pipe, pipe_status->ep_addr,
              (unsigned) usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.BYTE_COUNT,
              (unsigned) usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg,
              (unsigned) USB->HOST.HostPipe[pipe].PSTATUS.reg);
      xfer_result = XFER_RESULT_SUCCESS;
    }
    if (pint_flags & USB_HOST_PINTFLAG_TXSTP) {
      USB->HOST.HostPipe[pipe].PINTFLAG.reg = USB_HOST_PINTFLAG_TXSTP;
      xfer_result = XFER_RESULT_SUCCESS;
    }
    if (pint_flags & USB_HOST_PINTFLAG_STALL) {
      TU_LOG2("USB_HOST_PINTFLAG_STALL\r\n");
      USB->HOST.HostPipe[pipe].PINTFLAG.reg = USB_HOST_PINTFLAG_STALL;
      xfer_result = XFER_RESULT_STALLED;
    }
    if (pint_flags & USB_HOST_PINTFLAG_TRFAIL) {
      USB->HOST.HostPipe[pipe].PINTFLAG.reg = USB_HOST_PINTFLAG_TRFAIL;
      TU_LOG3("PIPE%d TRFAIL ep=%02X PINTFLAG=%02X STATUS_BK=%02X STATUS_PIPE=%04X PSTATUS=%02X PCFG=%02X bytecount=%u\r\n",
              pipe, pipe_status->ep_addr,
              (unsigned) USB->HOST.HostPipe[pipe].PINTFLAG.reg,
              (unsigned) usb_pipe_table[pipe].HostDescBank[0].STATUS_BK.reg,
              (unsigned) usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg,
              (unsigned) USB->HOST.HostPipe[pipe].PSTATUS.reg,
              (unsigned) USB->HOST.HostPipe[pipe].PCFG.reg,
              (unsigned) usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.BYTE_COUNT);
      if (usb_pipe_table[pipe].HostDescBank[0].STATUS_BK.reg & USB_HOST_STATUS_BK_ERRORFLOW) {
        // ERRORFLOW alone means NAK (flow control): the device isn't ready
        // yet — NOT a failure. The hardware froze the pipe; unfreeze to keep
        // polling like IN does. Genuine errors still fail via CRCERR below
        // or the STATUS_PIPE checks (STALL/DTGL/PID/CRC16/TOUT).
        // Without this, every control status-OUT NAKs once (device arms
        // EP0-OUT lazily after the data stage) and the whole transfer is
        // wrongly FAILED — deterministically fatal when the host task
        // outruns the device (fast/fast builds die on the 2nd descriptor).
        // Only resume if nothing else already decided the outcome (a
        // concurrent TRCPT/STALL must stand).
        if (xfer_result == XFER_RESULT_INVALID) {
          USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_PFREEZE;
        }
      } else if (usb_pipe_table[pipe].HostDescBank[0].STATUS_BK.reg & USB_HOST_STATUS_BK_CRCERR) {
        TU_LOG1("USB_HOST_STATUS_BK_CRCERR\r\n");
        xfer_result = XFER_RESULT_FAILED;
      } else {
        // SAMD Quirk #1:
        // Likes to report TRFAIL for no apparent reason -> ignore
      }
    }
    if (pint_flags & USB_HOST_PINTFLAG_PERR) {
      USB->HOST.HostPipe[pipe].PINTFLAG.reg = USB_HOST_PINTFLAG_PERR;
      // Handled by STATUS_PIPE checks below
    }

    //
    // Check STATUS_PIPE
    //
    if (usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg & USB_HOST_STATUS_PIPE_DTGLER) {
      TU_LOG1("USB_HOST_STATUS_PIPE_DTGLER\r\n");
      usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg &= ~USB_HOST_STATUS_PIPE_DTGLER;
      xfer_result = XFER_RESULT_FAILED;
    }
    if (usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg & USB_HOST_STATUS_PIPE_DAPIDER) {
      TU_LOG1("USB_HOST_STATUS_PIPE_DAPIDER\r\n");
      usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg &= ~USB_HOST_STATUS_PIPE_DAPIDER;
      xfer_result = XFER_RESULT_FAILED;
    }
    if (usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg & USB_HOST_STATUS_PIPE_PIDER) {
      TU_LOG1("USB_HOST_STATUS_PIPE_PIDER\r\n");
      usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg &= ~USB_HOST_STATUS_PIPE_PIDER;
      xfer_result = XFER_RESULT_FAILED;
    }
    if (usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg & USB_HOST_STATUS_PIPE_CRC16ER) {
      TU_LOG1("USB_HOST_STATUS_PIPE_CRC16ER\r\n");
      usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg &= ~USB_HOST_STATUS_PIPE_CRC16ER;
      xfer_result = XFER_RESULT_FAILED;
    }
    if (usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg & USB_HOST_STATUS_PIPE_TOUTER) {
      usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.reg &= ~USB_HOST_STATUS_PIPE_TOUTER;

      if ((USB->HOST.HostPipe[pipe].PCFG.bit.PTYPE == USB_HOST_PTYPE_INT) &&
          (tu_edpt_dir(pipe_status->ep_addr) == TUSB_DIR_IN)) {
        // ignore timeouts from INT pipes
      } else {
        if (xfer_result == XFER_RESULT_INVALID) {
          xfer_result = XFER_RESULT_TIMEOUT;
        }
      }
    }

    // prevent PERR from too high error counts, that is handled by TinyUSB anyways
    usb_pipe_table[pipe].HostDescBank[0].STATUS_PIPE.bit.ERCNT = 0;

    // no updates
    if (xfer_result == XFER_RESULT_INVALID) {
      continue;
    }

    // continue / complete transfer
    if (samd_on_xfer(pipe, xfer_result)) {
      hcd_event_xfer_complete(pipe_status->dev_addr, pipe_status->ep_addr, pipe_status->xfer_length, xfer_result, true);
    }
  }
}

// Initialize controller to host mode
bool hcd_init(uint8_t rhport, const tusb_rhport_init_t* rh_init) {
  TU_ASSERT(rhport == 0);
  (void) rh_init;
  fake_fnum = 0;

  // reset to get in a clean state.
  USB->HOST.CTRLA.bit.SWRST = 1;
  while (USB->HOST.SYNCBUSY.bit.SWRST == 0)
    ;
  while (USB->HOST.SYNCBUSY.bit.SWRST == 1)
    ;

  // load pad calibration
  USB->HOST.PADCAL.bit.TRANSP = (*((uint32_t*) USB_FUSES_TRANSP_ADDR) & USB_FUSES_TRANSP_Msk) >> USB_FUSES_TRANSP_Pos;
  USB->HOST.PADCAL.bit.TRANSN = (*((uint32_t*) USB_FUSES_TRANSN_ADDR) & USB_FUSES_TRANSN_Msk) >> USB_FUSES_TRANSN_Pos;
  USB->HOST.PADCAL.bit.TRIM = (*((uint32_t*) USB_FUSES_TRIM_ADDR) & USB_FUSES_TRIM_Msk) >> USB_FUSES_TRIM_Pos;

  USB->HOST.QOSCTRL.bit.CQOS = 3; // High Quality
  USB->HOST.QOSCTRL.bit.DQOS = 3; // High Quality

  // configure host-mode
  samd_free_all_pipes(); // initializes pipe handles and usb_pipe_table
  USB->HOST.DESCADD.reg = (uint32_t) (&usb_pipe_table[0]);
  USB->HOST.CTRLB.reg = USB_HOST_CTRLB_SPDCONF_NORMAL | USB_HOST_CTRLB_VBUSOK;
  USB->HOST.CTRLA.reg = USB_CTRLA_MODE_HOST | USB_CTRLA_ENABLE | USB_CTRLA_RUNSTDBY;
  while (USB->HOST.SYNCBUSY.bit.ENABLE == 1)
    ;

  // enable basic USB interrupts
  USB->HOST.INTFLAG.reg |= USB->HOST.INTFLAG.reg; // clear pending
  USB->HOST.INTENCLR.reg = USB_HOST_INTENCLR_MASK;
  USB->HOST.INTENSET.reg = USB_HOST_INTENSET_DCONN;
  USB->HOST.INTENSET.reg = USB_HOST_INTENSET_DDISC;
  USB->HOST.INTENSET.reg = USB_HOST_INTENSET_WAKEUP;
  USB->HOST.INTENSET.reg = USB_HOST_INTENSET_RST;

  return true;
}

#if TU_CHECK_MCU(OPT_MCU_SAMD51, OPT_MCU_SAME5X)

// Enable USB interrupt
void hcd_int_enable(uint8_t rhport)
{
  (void) rhport;
  NVIC_EnableIRQ(USB_0_IRQn);
  NVIC_EnableIRQ(USB_1_IRQn);
  NVIC_EnableIRQ(USB_2_IRQn);
  NVIC_EnableIRQ(USB_3_IRQn);
}

// Disable USB interrupt
void hcd_int_disable(uint8_t rhport)
{
  (void) rhport;
  NVIC_DisableIRQ(USB_3_IRQn);
  NVIC_DisableIRQ(USB_2_IRQn);
  NVIC_DisableIRQ(USB_1_IRQn);
  NVIC_DisableIRQ(USB_0_IRQn);
}

#elif TU_CHECK_MCU(OPT_MCU_SAMD11, OPT_MCU_SAMD21, OPT_MCU_SAML2X)

// Enable USB interrupt
void hcd_int_enable(uint8_t rhport)
{
  (void) rhport;
  NVIC_EnableIRQ(USB_IRQn);
}

// Disable USB interrupt
void hcd_int_disable(uint8_t rhport)
{
  (void) rhport;
  NVIC_DisableIRQ(USB_IRQn);
}

#else

#error "No implementation available for hcd_int_enable / hcd_int_disable"

#endif

// Get frame number (1ms)
uint32_t hcd_frame_number(uint8_t rhport)
{
  (void) rhport;

// SAMD Quirk #2:
// FNUM is stalled before enumeration of Low-Speed devices.
// internal frame counter can be used as workaround (not very accurate)
#ifdef HCD_SAMD_FAKE_FNUM
  uint8_t start, current, prev;
  uint8_t loop_count = (USB->HOST.STATUS.bit.SPEED == TUSB_SPEED_HIGH) ? 8 : 1;
  for (uint8_t i = 0; i < loop_count; i++) {
    start = USB->HOST.FLENHIGH.reg;
    current = start;
    // wait until wrap-around
    prev = current;
    while (current <= start) {
      current = USB->HOST.FLENHIGH.reg;
      if (current > prev)
        break;
      prev = current;
    }
    // wait until start is reached again
    prev = current;
    while (current > start) {
      current = USB->HOST.FLENHIGH.reg;
      if (current > prev)
        break;
      prev = current;
    }
  }
  fake_fnum += 1;
  return fake_fnum;
#else
  return USB->HOST.FNUM.bit.FNUM;
#endif // HCD_SAMD_FAKE_FNUM
}

//--------------------------------------------------------------------+
// Port API
//--------------------------------------------------------------------+

// Get the current connect status of roothub port
bool hcd_port_connect_status(uint8_t rhport)
{
  TU_ASSERT(rhport == 0);
  return USB->HOST.STATUS.bit.LINESTATE != 0;
}

// Reset USB bus on the port. Return immediately, bus reset sequence may not be
// complete. Some port would require hcd_port_reset_end() to be invoked after 10ms to
// complete the reset sequence.
void hcd_port_reset(uint8_t rhport)
{
  hcd_int_disable(rhport);
  samd_free_all_pipes();
  USB->HOST.INTFLAG.reg |= USB->HOST.INTFLAG.reg; // clear pending
  USB->HOST.CTRLB.bit.BUSRESET = 1;
  _samd_busreset_done = false;
  fake_fnum = 0;
}

// Complete bus reset sequence, may be required by some controllers
void hcd_port_reset_end(uint8_t rhport)
{
  (void) rhport;
  // Wait for the reset to complete. Two things matter here:
  //  - Wait on the millisecond clock, not a spin count. usbh.c already waits
  //    T(DRSTr) = 50 ms (ENUM_RESET_ROOT_DELAY_MS) before calling us, so the
  //    common case returns instantly; the bound only limits a genuine stall.
  //    The old 100000-iteration spin was ~6 ms at the SAMD21's 48 MHz but only
  //    ~1.7 ms at the GCM4's 120 MHz — a clock-dependent bug.
  //  - Accept the software latch as well as the hardware flag, because the
  //    OPT_OS_NONE event-queue spinlock re-enables the USB IRQs during this
  //    window and the ISR then clears RST (see _samd_busreset_done).
  uint32_t const start_ms = tusb_time_millis_api();
  while (!(_samd_busreset_done || USB->HOST.INTFLAG.bit.RST)) {
    if ((tusb_time_millis_api() - start_ms) > SAMD_BUSRESET_WAIT_MS) break;
  }
  if (!_samd_busreset_done && USB->HOST.INTFLAG.bit.RST == 0) {
    // Genuinely never completed (no device, or a bus fault) — re-issue once.
    TU_LOG1("hcd_port_reset_end: RST timeout, re-issuing BUSRESET\r\n");
    USB->HOST.INTFLAG.reg |= USB->HOST.INTFLAG.reg; // clear all pending
    USB->HOST.CTRLB.bit.BUSRESET = 1;
    uint32_t const retry_ms = tusb_time_millis_api();
    while (!(_samd_busreset_done || USB->HOST.INTFLAG.bit.RST)) {
      if ((tusb_time_millis_api() - retry_ms) > SAMD_BUSRESET_WAIT_MS) break;
    }
  }
  _samd_busreset_done = false;
  USB->HOST.INTFLAG.reg = USB_HOST_INTFLAG_RST;
  USB->HOST.CTRLB.bit.SOFE = 1;
  hcd_int_enable(rhport);
}

// Get port link speed
tusb_speed_t hcd_port_speed_get(uint8_t rhport)
{
  (void) rhport;

  switch (USB->HOST.STATUS.bit.SPEED) {
  case 0:
    return TUSB_SPEED_FULL;
  case 1:
    return TUSB_SPEED_LOW;
  case 2:
    return TUSB_SPEED_HIGH;
  default:
    return TUSB_SPEED_INVALID;
  }
}

// HCD closes all opened endpoints belong to this device
void hcd_device_close(uint8_t rhport, uint8_t dev_addr)
{
  (void) rhport;

  for (uint8_t pipe = 0; pipe < USB_PIPE_NUM; pipe++) {
    volatile usb_pipe_status_t* pipe_status = &usb_pipe_status_table[pipe];
    if (pipe_status->dev_addr == dev_addr) {
      samd_free_pipe(pipe);
    }
  }
}

//--------------------------------------------------------------------+
// Endpoints API
//--------------------------------------------------------------------+

// Open an endpoint
bool hcd_edpt_open(uint8_t rhport, uint8_t dev_addr, tusb_desc_endpoint_t const* ep_desc)
{
  TU_ASSERT(rhport == 0);

  uint8_t pipe;
  volatile usb_pipe_status_t* pipe_status;
  const uint8_t ep_addr = ep_desc->bEndpointAddress;
  const uint8_t bmAttributes = (ep_desc->bmAttributes.xfer)       |
                              ((ep_desc->bmAttributes.sync) << 2) |
                              ((ep_desc->bmAttributes.usage) << 4);

  // configure the pipe
  pipe = samd_configure_pipe(dev_addr, ep_addr, false);
  if (pipe >= USB_PIPE_NUM) {
    return false;
  }

  // initial configuration
  pipe_status = &usb_pipe_status_table[pipe];
  USB->HOST.HostPipe[pipe].PCFG.reg &= ~USB_HOST_PCFG_PTYPE_Msk;
  // PTYPE takes the transfer-type bits only (00=CTRL 01=ISO 10=BULK 11=INT).
  // Using the full bmAttributes byte misprograms the pipe for any endpoint
  // with sync/usage bits set: ISO-async (0x05, standard UAC2 streaming EP)
  // wrote PTYPE=6 (reserved) instead of ISO=2, and the pipe DTGLER-aborted
  // the second IN packet (device correctly sends DATA0 with no toggle).
  // Observed on the wire: IN→ZLP DATA0→ACK, IN→98B DATA0→ACK, then DTGLER.
  USB->HOST.HostPipe[pipe].PCFG.bit.PTYPE = (bmAttributes & 0x03) + 1;
  USB->HOST.HostPipe[pipe].BINTERVAL.reg = ep_desc->bInterval;
  USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_DTGL;
  USB->HOST.HostPipe[pipe].PINTENCLR.reg = USB_HOST_PINTENCLR_MASK;
  pipe_status->max_packet_size = ep_desc->wMaxPacketSize;
  usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.SIZE = USB_PCKSIZE_ENUM(pipe_status->max_packet_size);
  usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.AUTO_ZLP = 0;

  return true;
}

// Close an endpoint: release its pipe. Required by the audio host
// driver (tuh_edpt_close); missing from upstream SAMD HCD (link error
// in any audio-host build). Mirrors hcd_sam3x.c semantics.
bool hcd_edpt_close(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr)
{
  TU_ASSERT(rhport == 0);

  for (uint8_t pipe = 0; pipe < USB_PIPE_NUM; pipe++) {
    volatile usb_pipe_status_t* pipe_status = &usb_pipe_status_table[pipe];
    if (pipe_status->dev_addr == dev_addr && pipe_status->ep_addr == ep_addr) {
      samd_free_pipe(pipe);
      return true;
    }
  }
  return false;
}

// Submit a special transfer to send 8-byte Setup Packet, when complete
// hcd_event_xfer_complete() must be invoked
bool hcd_setup_send(uint8_t rhport, uint8_t dev_addr, uint8_t const setup_packet[8])
{
  TU_ASSERT(rhport == 0);

  uint8_t pipe;
  volatile usb_pipe_status_t* pipe_status;

  // configure the pipe
  pipe = samd_configure_pipe(dev_addr, 0, true);
  if (pipe >= USB_PIPE_NUM) {
    return false;
  }

  // prepare transfer
  pipe_status = &usb_pipe_status_table[pipe];
  usb_pipe_table[pipe].HostDescBank[0].ADDR.reg = (uint32_t) setup_packet;
  pipe_status->xfer_remaining = 8;
  pipe_status->xfer_length = 0;
  // SAMD21 quirk: after SETUP, next IN data phase must use DATA1 and
  // OUT status phase must use DATA1. Hardware auto-toggle doesn't work.
  pipe_status->bmRcvToggle = 1;
  pipe_status->bmSndToggle = 1;
  usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.BYTE_COUNT = 8;
  usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.MULTI_PACKET_SIZE = 0;
  USB->HOST.HostPipe[pipe].PSTATUSSET.reg = USB_HOST_PSTATUSSET_BK0RDY;

  // clear pending interrupts
  USB->HOST.HostPipe[pipe].PINTFLAG.reg |= USB->HOST.HostPipe[pipe].PINTFLAG.reg;

  // begin transfer
  USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_PFREEZE;

  return true;
}

// Submit a transfer, when complete hcd_event_xfer_complete() must be invoked
bool hcd_edpt_xfer(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr, uint8_t* buffer, uint16_t buflen)
{
  TU_ASSERT(rhport == 0);

  uint8_t pipe;
  volatile usb_pipe_status_t* pipe_status;

  // configure the pipe
  pipe = samd_configure_pipe(dev_addr, ep_addr, false);
  if (pipe >= USB_PIPE_NUM) {
    return false;
  }

  // prepare transfer
  pipe_status = &usb_pipe_status_table[pipe];
  usb_pipe_table[pipe].HostDescBank[0].ADDR.reg = (uint32_t) buffer;
  pipe_status->xfer_remaining = buflen;
  pipe_status->xfer_length = 0;
  // receive data
  if (tu_edpt_dir(pipe_status->ep_addr) == TUSB_DIR_IN) {
    // SAMD21 quirk: hardware auto-toggle broken — set toggle from software state.
    // This workaround is OURS, not upstream (upstream hcd_samd.c has no
    // toggle tracking at all, so it relies on the auto-toggle that SAMD21
    // does not perform — every control-data IN fails DTGLER there).
    // EXCEPT on ISO pipes: isochronous has no data toggle (device sends DATA0
    // every packet), so leave DTGL cleared — setting it from the alternating
    // software state DTGLER-aborts every 2nd packet (observed on hardware).
    // NOTE: with DTGL pinned 0 the SAMD21 still aborts the 2nd DATA0, i.e.
    // SAMD21 enforces DATA alternation even on ISO pipes.
    bool const is_iso =
        (USB->HOST.HostPipe[pipe].PCFG.bit.PTYPE == USB_HOST_PTYPE_ISO);
    if (!is_iso && SAMD_HOST_SW_TOGGLE) {
      if (pipe_status->bmRcvToggle)
        USB->HOST.HostPipe[pipe].PSTATUSSET.reg = USB_HOST_PSTATUSSET_DTGL;
      else
        USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_DTGL;
    } else if (is_iso) {
      USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_DTGL;
    }
    usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.BYTE_COUNT = 0;
    usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.MULTI_PACKET_SIZE = pipe_status->max_packet_size;
    USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_BK0RDY;
  }
  // send data
  else {
    // SAMD21 quirk: hardware auto-toggle broken — set toggle from software state
    // (ISO bypass as above: no toggle on isochronous).
    bool const is_iso =
        (USB->HOST.HostPipe[pipe].PCFG.bit.PTYPE == USB_HOST_PTYPE_ISO);
    if (!is_iso && SAMD_HOST_SW_TOGGLE) {
      if (pipe_status->bmSndToggle)
        USB->HOST.HostPipe[pipe].PSTATUSSET.reg = USB_HOST_PSTATUSSET_DTGL;
      else
        USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_DTGL;
    } else if (is_iso) {
      USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_DTGL;
    }
    usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.BYTE_COUNT =
        (pipe_status->xfer_remaining < pipe_status->max_packet_size) ? pipe_status->xfer_remaining
                                                                     : pipe_status->max_packet_size;
    usb_pipe_table[pipe].HostDescBank[0].PCKSIZE.bit.MULTI_PACKET_SIZE = 0;
    USB->HOST.HostPipe[pipe].PSTATUSSET.reg = USB_HOST_PSTATUSSET_BK0RDY;
  }

  // clear pending interrupts
  USB->HOST.HostPipe[pipe].PINTFLAG.reg |= USB->HOST.HostPipe[pipe].PINTFLAG.reg;

  // begin transfer
  USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_PFREEZE;

  return true;
}

// Abort a queued transfer. Note: it can only abort transfer that has not been
// started Return true if a queued transfer is aborted, false if there is no transfer
// to abort
bool hcd_edpt_abort_xfer(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr)
{
  TU_ASSERT(rhport == 0);

  uint8_t pipe;
  volatile usb_pipe_status_t* pipe_status;

  TU_LOG3("hcd_edpt_abort_xfer(dev_addr=%02X, ep_addr=%02X)=", dev_addr, ep_addr);

  // find the pipe
  for (pipe = 0; pipe < USB_PIPE_NUM; pipe++) {
    pipe_status = &usb_pipe_status_table[pipe];
    if ((pipe_status->dev_addr == dev_addr) && (pipe_status->ep_addr == ep_addr)) {
      break;
    }
  }

  // pipe not found
  if (pipe >= USB_PIPE_NUM) {
    TU_LOG3("ERR_NO_PIPE\r\n");
    return false;
  }
  TU_LOG3("%d\r\n", pipe);

  // no transfer in progress
  if (USB->HOST.HostPipe[pipe].PSTATUS.bit.PFREEZE == 1) {
    return false;
  }

  // abort the transfer
  USB->HOST.HostPipe[pipe].PSTATUSSET.reg = USB_HOST_PSTATUSSET_PFREEZE;
  pipe_status = &usb_pipe_status_table[pipe];
  pipe_status->xfer_length = 0;
  pipe_status->xfer_remaining = 0;

  return true;
}

// clear stall, data toggle is also reset to DATA0
bool hcd_edpt_clear_stall(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr)
{
  TU_ASSERT(rhport == 0);

  uint8_t pipe;
  volatile usb_pipe_status_t* pipe_status;

  TU_LOG3("hcd_edpt_clear_stall(dev_addr=%02X, ep_addr=%02X)=", dev_addr, ep_addr);

  // find the pipe
  for (pipe = 0; pipe < USB_PIPE_NUM; pipe++) {
    pipe_status = &usb_pipe_status_table[pipe];
    if ((pipe_status->dev_addr == dev_addr) && (pipe_status->ep_addr == ep_addr)) {
      break;
    }
  }

  // pipe not found
  if (pipe >= USB_PIPE_NUM) {
    TU_LOG3("ERR_NO_PIPE\r\n");
    return false;
  }
  TU_LOG3("%d\r\n", pipe);

  // clear pending interrupts
  USB->HOST.HostPipe[pipe].PINTFLAG.reg |= USB->HOST.HostPipe[pipe].PINTFLAG.reg;

  // clear stalled state
  USB->HOST.HostPipe[pipe].PSTATUSSET.reg = USB_HOST_PSTATUSSET_PFREEZE;
  USB->HOST.HostPipe[pipe].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_DTGL;

  return true;
}

#endif
