// hcd_sam3x.c — TinyUSB host driver for SAM3X8E UOTGHS.
//
// Register sequences follow libsam uotghs_host.c (pipe CFG/address/token,
// FIFO access, polled completion bits) adapted to interrupt-driven TinyUSB
// HCD flow. SAME70's samx7x has no HCD (UDPHS device-only there), so there is
// no donor; libsam's polled UHD_Pipe_* is the reference.
// Design mirrors dcd_sam3x.c lessons: transfer-gated pipe interrupts,
// exact-length transfers, never touch flags without an active transfer.
//
// Pipe model: 10 pipes, 0 = control (reconfigured per daddr/size on every
// setup/data stage — TinyUSB has no EP0 MPS param, so MPS follows bus speed:
// LS = 8, else 64), 1..9 = one pipe per open (daddr, endpoint) — required
// because UOTGHS cannot change data toggles manually (see libsam note).
#include "tusb_option.h"

#if CFG_TUH_ENABLED && (CFG_TUSB_MCU == OPT_MCU_SAM3X)

#include "host/hcd.h"
#include "chip.h"

#define PIPE_MAX 10
#define PIPE_CTRL 0
#define HCD_MAX_RETRY 3
// Timeout for any single pipe transfer (ms). If the ISR sees an active
// transfer older than this, it forces completion with FAILED. Enumeration
// GET_DESCRIPTOR can be slow on Low-Speed devices; 5 s is generous.
#define HCD_XFER_TIMEOUT_MS  5000

typedef struct {
  uint8_t daddr;
  uint8_t ep_addr;
  uint8_t type;   // TUSB_XFER_* (control/bulk/intr/iso)
  uint16_t mps;
  uint8_t *buf;
  uint16_t total;
  uint16_t queued;
  uint8_t retries;
  bool active;
  uint32_t start_ms;   // timestamp when transfer started (tusb_time_millis_api)
} pipe_xfer_t;

static pipe_xfer_t _pipes[PIPE_MAX];
static bool _connected;
static tusb_speed_t _port_speed = TUSB_SPEED_FULL;

// Mini ring log: (code, pipe, total, queued). code: 1=setup kick, 2=IN kick,
// 3=OUT kick, 4=TXSTP done, 5=RXINI done, 6=TXOUTI done, 7=stall, 8=nak,
// 9=error-retry, 10=error-fail, 11=attach, 12=remove, 13=reset start/end.
#define HRING_N 64
static volatile uint32_t _hring[HRING_N];
static volatile uint8_t _hring_w;
static void hring_log(uint8_t code, uint8_t p, uint16_t total, uint16_t queued) {
  _hring[_hring_w] = ((uint32_t) code << 24) | ((uint32_t) p << 16) |
      ((uint32_t)(total & 0xFF) << 8) | (queued & 0xFF);
  _hring_w = (_hring_w + 1) % HRING_N;
}

void hcd_sam3x_ring(uint32_t *out64) {
  for (int i = 0; i < HRING_N; i++) out64[i] = _hring[i];
}

static uint32_t psize_bits(uint16_t mps) {
  if (mps <= 8) return UOTGHS_HSTPIPCFG_PSIZE_8_BYTE;
  if (mps <= 16) return UOTGHS_HSTPIPCFG_PSIZE_16_BYTE;
  if (mps <= 32) return UOTGHS_HSTPIPCFG_PSIZE_32_BYTE;
  if (mps <= 64) return UOTGHS_HSTPIPCFG_PSIZE_64_BYTE;
  if (mps <= 128) return UOTGHS_HSTPIPCFG_PSIZE_128_BYTE;
  if (mps <= 256) return UOTGHS_HSTPIPCFG_PSIZE_256_BYTE;
  if (mps <= 512) return UOTGHS_HSTPIPCFG_PSIZE_512_BYTE;
  return UOTGHS_HSTPIPCFG_PSIZE_1024_BYTE;
}

static uint16_t psize_bytes(uint32_t cfg) {
  return (uint16_t) (8u << ((cfg & UOTGHS_HSTPIPCFG_PSIZE_Msk) >> UOTGHS_HSTPIPCFG_PSIZE_Pos));
}

static uint32_t ptype_bits(uint8_t xfer) {
  switch (xfer) {
    case TUSB_XFER_ISOCHRONOUS: return UOTGHS_HSTPIPCFG_PTYPE_ISO;
    case TUSB_XFER_BULK: return UOTGHS_HSTPIPCFG_PTYPE_BLK;
    case TUSB_XFER_INTERRUPT: return UOTGHS_HSTPIPCFG_PTYPE_INTRPT;
    default: return UOTGHS_HSTPIPCFG_PTYPE_CTRL;
  }
}

static void pipe_write_fifo(uint8_t p, uint8_t const *src, uint16_t len) {
  volatile uint8_t *dst = (volatile uint8_t *) &uhd_get_pipe_fifo_access(p, 8);
  for (uint16_t i = 0; i < len; i++) dst[i] = src[i];
}

static void pipe_read_fifo(uint8_t p, uint8_t *dst, uint16_t len) {
  volatile uint8_t *src = (volatile uint8_t *) &uhd_get_pipe_fifo_access(p, 8);
  for (uint16_t i = 0; i < len; i++) dst[i] = src[i];
}

// Enable the completion/error interrupts for an active transfer.
// NOTE: NAK interrupt stays OFF on purpose. NAKs are routine (idle interrupt
// polls NAK at the poll rate) and the pipe auto-retries on SOF schedule;
// enabling NAKEDI floods the ISR + trace with thousands of no-op visits per
// second (observed: whole 64-entry ring = NAKs). A device NAKing bulk forever
// is bounded by usbh timeouts + abort (which freezes the pipe).
static void pipe_int_enable(uint8_t p, bool is_setup) {
  if (is_setup) {
    UOTGHS->UOTGHS_HSTPIPIER[p] = UOTGHS_HSTPIPIER_TXSTPES;
  } else if (uhd_is_pipe_in(p)) {
    UOTGHS->UOTGHS_HSTPIPIER[p] = UOTGHS_HSTPIPIER_RXINES;
  } else {
    UOTGHS->UOTGHS_HSTPIPIER[p] = UOTGHS_HSTPIPIER_TXOUTES;
  }
  UOTGHS->UOTGHS_HSTPIPIER[p] = UOTGHS_HSTPIPIER_RXSTALLDES;
}

static void pipe_int_disable_all(uint8_t p) {
  UOTGHS->UOTGHS_HSTPIPIDR[p] = UOTGHS_HSTPIPIDR_TXSTPEC | UOTGHS_HSTPIPIDR_RXINEC |
      UOTGHS_HSTPIPIDR_TXOUTEC | UOTGHS_HSTPIPIDR_RXSTALLDEC;
}

// Configure pipe 0 for a control stage to daddr (address; EP0 size is the
// stored per-open MPS, falling back to speed-based default).
static void pipe0_setup(uint8_t daddr) {
  uint16_t mps = _pipes[PIPE_CTRL].mps;
  if (mps == 0) mps = (_port_speed == TUSB_SPEED_LOW) ? 8 : 64;
  uhd_enable_pipe(0);
  uhd_configure_pipe(0, 0, 0, UOTGHS_HSTPIPCFG_PTYPE_CTRL,
      UOTGHS_HSTPIPCFG_PTOKEN_SETUP, mps, UOTGHS_HSTPIPCFG_PBK_1_BANK, 0);
  uhd_allocate_memory(0);
  uhd_configure_address(0, daddr);
  _pipes[0].mps = psize_bytes(UOTGHS->UOTGHS_HSTPIPCFG[0]);
  if (_pipes[0].mps > mps) _pipes[0].mps = mps;
}

// Kick the next packet of an IN transfer. Returns true when done (event owed).
static bool pipe_in_step(uint8_t rhport, uint8_t p) {
  pipe_xfer_t *x = &_pipes[p];
  if (!x->active) return false;
  // RXINI means a packet is banked; short packet (or full take) ends it.
  // For Low-Speed devices, the host ACK may still be in flight when RXINI
  // fires. Wait for the pipe to fully freeze before touching the FIFO,
  // matching libsam UHD_Pipe_Is_Transfer_Complete.
  while (!(UOTGHS->UOTGHS_HSTPIPIMR[p] & UOTGHS_HSTPIPIMR_PFREEZE)) {}
  uint16_t byct = uhd_byte_count(p);
  uint16_t remaining = (x->total > x->queued) ? x->total - x->queued : 0;
  uint16_t n = (byct < remaining) ? byct : remaining;
  if (n && x->buf) pipe_read_fifo(p, x->buf + x->queued, n);
  x->queued += n;
  hring_log(14, p, x->queued, byct);
  uhd_ack_in_received(p);
  // NOTE: Do NOT toggle FIFOCON here. FIFOCON is toggled exactly once per
  // packet in the re-arm path (matching libsam UHD_Pipe_Send). Toggling it
  // here AND in the re-arm would double-toggle it back to "bank filled",
  // preventing the hardware from scheduling the next IN token.
  if (x->queued >= x->total || byct < x->mps) {    uint16_t done = x->queued;
    uint8_t daddr = x->daddr, ep_addr = x->ep_addr;
    x->active = false;
    pipe_int_disable_all(p);
    uhd_freeze_pipe(p);
    hring_log(5, p, done, byct);
    hcd_event_xfer_complete(daddr, ep_addr, done, XFER_RESULT_SUCCESS, true);
    (void) rhport;
    return true;
  }
  // More packets expected: re-trigger like libsam UHD_Pipe_Send (token
  // rewrite + bank release + unfreeze). Match the exact libsam sequence:
  // 1. Configure token, 2. Clear all flags, 3. Toggle FIFOCON, 4. Unfreeze.
  uhd_configure_pipe_token(p, UOTGHS_HSTPIPCFG_PTOKEN_IN);
  uhd_ack_setup_ready(p);
  uhd_ack_in_received(p);
  uhd_ack_out_ready(p);
  uhd_ack_short_packet(p);
  uhd_ack_nak_received(p);
  uhd_ack_fifocon(p);
  uhd_unfreeze_pipe(p);
  return false;
}

//--------------------------------------------------------------------+
// Controller API
//--------------------------------------------------------------------+

bool hcd_init(uint8_t rhport, const tusb_rhport_init_t *rh_init) {
  (void) rhport; (void) rh_init;
  for (int i = 0; i < PIPE_MAX; i++) {
    _pipes[i].daddr = 0; _pipes[i].ep_addr = 0; _pipes[i].type = 0;
    _pipes[i].buf = NULL; _pipes[i].total = _pipes[i].queued = 0;
    _pipes[i].mps = 64; _pipes[i].retries = 0; _pipes[i].active = false;
  }
  _connected = false;
  // SOF drives pipe scheduling + frame numbers; DCONN arms attach detect;
  // PEP_0 arms pipe-0 (control) interrupts — pipe 0 has no hcd_edpt_open to
  // do it (configured per-transfer), other pipes enable theirs on open.
  // SOFIE provides a 1 ms heartbeat so hcd_check_timeouts() runs even when
  // a Low-Speed device NAKs indefinitely with NAKEDI disabled (no pipe ISR).
  uhd_enable_sof();
  UOTGHS->UOTGHS_HSTIER = UOTGHS_HSTIER_DCONNIES | UOTGHS_HSTIER_PEP_0 |
      UOTGHS_HSTIER_HSOFIES;
  // A device already plugged at boot produces no DCONN edge: if the flag is
  // sitting set (connection established while we initialized VBUS/SOF),
  // synthesize the attach now or the device is missed forever.
  if (Is_uhd_connection()) {
    uhd_ack_connection();
    UOTGHS->UOTGHS_HSTIDR = UOTGHS_HSTIDR_DCONNIEC;
    uhd_ack_disconnection();
    UOTGHS->UOTGHS_HSTIER = UOTGHS_HSTIER_DDISCIES | UOTGHS_HSTIER_PEP_0;
    if (Is_uhd_low_speed_mode()) _port_speed = TUSB_SPEED_LOW;
    else if (Is_uhd_high_speed_mode()) _port_speed = TUSB_SPEED_HIGH;
    else _port_speed = TUSB_SPEED_FULL;
    _connected = true;
    hring_log(11, 0, (uint16_t) _port_speed, 0);
    hcd_event_device_attach(rhport, false);
  }
  return true;
}

bool hcd_deinit(uint8_t rhport) {
  (void) rhport;
  return true;
}

void hcd_int_enable(uint8_t rhport) {
  (void) rhport;
  NVIC_EnableIRQ(UOTGHS_IRQn);
}

void hcd_int_disable(uint8_t rhport) {
  (void) rhport;
  NVIC_DisableIRQ(UOTGHS_IRQn);
}

void hcd_int_handler(uint8_t rhport, bool in_isr) {
  // Not used: hcd_sam3x_isr is hooked via UHD_SetStack (libsam UOTGHS_Handler
  // dispatches gpf_isr — no link conflict with the SAM core).
  (void) rhport; (void) in_isr;
}

uint32_t hcd_frame_number(uint8_t rhport) {
  (void) rhport;
  return uhd_get_sof_number();
}

//--------------------------------------------------------------------+
// Port API
//--------------------------------------------------------------------+

bool hcd_port_connect_status(uint8_t rhport) {
  (void) rhport;
  return _connected;
}

void hcd_port_reset(uint8_t rhport) {
  (void) rhport;
  hring_log(13, 0, 1, 0);
  uhd_start_reset();
}

void hcd_port_reset_end(uint8_t rhport) {
  (void) rhport;
  hring_log(13, 0, 0, 0);
  uhd_stop_reset();
  uhd_ack_reset_sent();
}

tusb_speed_t hcd_port_speed_get(uint8_t rhport) {
  (void) rhport;
  return _port_speed;
}

void hcd_device_close(uint8_t rhport, uint8_t dev_addr) {
  (void) rhport;
  for (int p = 1; p < PIPE_MAX; p++) {
    if (_pipes[p].daddr == dev_addr) {
      pipe_int_disable_all((uint8_t) p);
      uhd_disable_pipe(p);
      uhd_unallocate_memory(p);
      _pipes[p].daddr = 0; _pipes[p].ep_addr = 0;
      _pipes[p].buf = NULL; _pipes[p].total = _pipes[p].queued = 0;
      _pipes[p].active = false;
    }
  }
}

//--------------------------------------------------------------------+
// Endpoints API
//--------------------------------------------------------------------+

bool hcd_edpt_open(uint8_t rhport, uint8_t daddr, tusb_desc_endpoint_t const *ep_desc) {
  (void) rhport;
  uint8_t epnum = tu_edpt_number(ep_desc->bEndpointAddress);
  uint8_t dir = tu_edpt_dir(ep_desc->bEndpointAddress);
  uint16_t mps = tu_edpt_packet_size(ep_desc);
  if (epnum == 0) {
    // Pipe 0 serves all control transfers and is (re)configured per daddr on
    // every setup_send/data xfer — but the EP0 max-packet-size comes from
    // here: usbh opens EP0 first with 8, then with the device's actual
    // bMaxPacketSize0 (critical: an 8-MPS device on a 64-MPS pipe completes
    // after the first packet as "short", truncating descriptors).
    _pipes[PIPE_CTRL].mps = mps ? mps : _pipes[PIPE_CTRL].mps;
    (void) dir;
    return true;
  }
  uint32_t ptype;
  switch (ep_desc->bmAttributes.xfer) {
    case TUSB_XFER_ISOCHRONOUS: ptype = UOTGHS_HSTPIPCFG_PTYPE_ISO; break;
    case TUSB_XFER_BULK: ptype = UOTGHS_HSTPIPCFG_PTYPE_BLK; break;
    case TUSB_XFER_INTERRUPT: ptype = UOTGHS_HSTPIPCFG_PTYPE_INTRPT; break;
    default: return false;
  }
  // Reuse an existing identical pipe if usbh re-opens (idempotent open).
  for (int p = 1; p < PIPE_MAX; p++) {
    if (_pipes[p].daddr == daddr && _pipes[p].ep_addr == ep_desc->bEndpointAddress) return true;
  }
  for (int p = 1; p < PIPE_MAX; p++) {
    if (Is_uhd_pipe_enabled(p)) continue;
    uint32_t token = (dir == TUSB_DIR_IN) ? UOTGHS_HSTPIPCFG_PTOKEN_IN : UOTGHS_HSTPIPCFG_PTOKEN_OUT;
    uhd_enable_pipe(p);
    uhd_configure_pipe(p, ep_desc->bInterval, epnum, ptype, token, mps,
        UOTGHS_HSTPIPCFG_PBK_1_BANK, 0);
    uhd_allocate_memory(p);
    for (volatile uint32_t i = 0; i < 10000; i++) {
      if (Is_uhd_pipe_configured(p)) break;
    }
    // Proceed regardless (DCD lesson: slow CFGOK must not fail opens).
    uhd_configure_address(p, daddr);
    uhd_reset_data_toggle(p);
    _pipes[p].daddr = daddr;
    _pipes[p].ep_addr = ep_desc->bEndpointAddress;
    _pipes[p].type = ep_desc->bmAttributes.xfer;
    _pipes[p].mps = psize_bytes(UOTGHS->UOTGHS_HSTPIPCFG[p]);
    if (_pipes[p].mps > mps) _pipes[p].mps = mps;
    _pipes[p].buf = NULL; _pipes[p].total = _pipes[p].queued = 0;
    _pipes[p].active = false;
    UOTGHS->UOTGHS_HSTIER = (UOTGHS_HSTIER_PEP_0 << p);
    return true;
  }
  return false;
}

bool hcd_edpt_close(uint8_t rhport, uint8_t daddr, uint8_t ep_addr) {
  (void) rhport;
  for (int p = 1; p < PIPE_MAX; p++) {
    if (_pipes[p].daddr == daddr && _pipes[p].ep_addr == ep_addr) {
      pipe_int_disable_all((uint8_t) p);
      uhd_disable_pipe(p);
      uhd_unallocate_memory(p);
      _pipes[p].daddr = 0; _pipes[p].ep_addr = 0;
      _pipes[p].buf = NULL; _pipes[p].total = _pipes[p].queued = 0;
      _pipes[p].active = false;
      return true;
    }
  }
  return true;
}

static int find_pipe(uint8_t daddr, uint8_t ep_addr) {
  if (tu_edpt_number(ep_addr) == 0) return PIPE_CTRL;
  for (int p = 1; p < PIPE_MAX; p++) {
    if (_pipes[p].daddr == daddr && _pipes[p].ep_addr == ep_addr) return p;
  }
  return -1;
}

bool hcd_edpt_xfer(uint8_t rhport, uint8_t daddr, uint8_t ep_addr, uint8_t *buffer, uint16_t buflen) {
  int pi = find_pipe(daddr, ep_addr);
  if (pi < 0) return false;
  uint8_t p = (uint8_t) pi;
  pipe_xfer_t *x = &_pipes[p];
  if (x->active) return false; // one outstanding per pipe; usbh serializes
  if (tu_edpt_number(ep_addr) == 0) {
    pipe0_setup(daddr);
    x->daddr = daddr; x->ep_addr = ep_addr; x->type = TUSB_XFER_CONTROL;
  }
  x->buf = buffer; x->total = buflen; x->queued = 0; x->retries = 0;
  x->active = true;
  x->start_ms = tusb_time_millis_api();
  if (tu_edpt_dir(ep_addr) == TUSB_DIR_IN) {
    hring_log(2, p, buflen, 0);
    uhd_configure_pipe_token(p, UOTGHS_HSTPIPCFG_PTOKEN_IN);
    // Clear ALL pipe flags (matching libsam UHD_Pipe_Send) to avoid stale
    // state from the preceding SETUP stage interfering with the IN data phase.
    uhd_ack_setup_ready(p);
    uhd_ack_in_received(p);
    uhd_ack_out_ready(p);
    uhd_ack_short_packet(p);
    uhd_ack_nak_received(p);
    uhd_ack_fifocon(p);
    pipe_int_enable(p, false);
    uhd_unfreeze_pipe(p);
  } else {
    hring_log(3, p, buflen, 0);
    uhd_configure_pipe_token(p, UOTGHS_HSTPIPCFG_PTOKEN_OUT);
    uint16_t n = (buflen < x->mps) ? buflen : x->mps;
    if (n && buffer) pipe_write_fifo(p, buffer, n);
    x->queued = n;
    pipe_int_enable(p, false);
    uhd_ack_out_ready(p);
    uhd_ack_fifocon(p);
    uhd_unfreeze_pipe(p);
  }
  (void) rhport;
  return true;
}

bool hcd_edpt_abort_xfer(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr) {
  (void) rhport;
  int pi = find_pipe(dev_addr, ep_addr);
  if (pi < 0) return false;
  pipe_xfer_t *x = &_pipes[pi];
  if (!x->active) return false;
  x->active = false;
  pipe_int_disable_all((uint8_t) pi);
  uhd_freeze_pipe(pi);
  return true;
}

bool hcd_setup_send(uint8_t rhport, uint8_t daddr, uint8_t const setup_packet[8]) {
  pipe0_setup(daddr);
  pipe_xfer_t *x = &_pipes[PIPE_CTRL];
  x->daddr = daddr; x->ep_addr = 0; x->type = TUSB_XFER_CONTROL;
  x->buf = NULL; x->total = 8; x->queued = 0; x->retries = 0;
  x->active = true;
  x->start_ms = tusb_time_millis_api();
  pipe_write_fifo(PIPE_CTRL, setup_packet, 8);
  x->queued = 8;
  hring_log(1, 0, daddr, 0);
  uhd_configure_pipe_token(PIPE_CTRL, UOTGHS_HSTPIPCFG_PTOKEN_SETUP);
  pipe_int_enable(PIPE_CTRL, true);
  uhd_ack_setup_ready(PIPE_CTRL);
  uhd_ack_fifocon(PIPE_CTRL);
  uhd_unfreeze_pipe(PIPE_CTRL);
  (void) rhport;
  return true;
}

bool hcd_edpt_clear_stall(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr) {
  (void) rhport;
  int pi = find_pipe(dev_addr, ep_addr);
  if (pi < 0) return false;
  uhd_reset_data_toggle(pi);
  uhd_unfreeze_pipe(pi);
  _pipes[pi].active = false;
  return true;
}

//--------------------------------------------------------------------+
// ISR
//--------------------------------------------------------------------+

// Check all active pipes for transfer timeouts. Called from ISR so it runs
// even when the targeted pipe never fires an interrupt (common with LS devices
// that NAK indefinitely with NAKEDI disabled).
static void hcd_check_timeouts(uint8_t rhport) {
  uint32_t now = tusb_time_millis_api();
  for (uint8_t p = 0; p < PIPE_MAX; p++) {
    pipe_xfer_t *x = &_pipes[p];
    if (!x->active) continue;
    if ((now - x->start_ms) < HCD_XFER_TIMEOUT_MS) continue;
    // Timed out: freeze pipe, disable interrupts, report failure.
    uint8_t daddr = x->daddr, ep_addr = x->ep_addr;
    x->active = false;
    pipe_int_disable_all(p);
    uhd_freeze_pipe(p);
    hring_log(10, p, 0x80 | p, 0);  // code 10 = error-fail, subcode=pipe
    hcd_event_xfer_complete(daddr, ep_addr, 0, XFER_RESULT_FAILED, true);
  }
}

static void hcd_pipe_isr(uint8_t rhport, uint8_t p) {
  pipe_xfer_t *x = &_pipes[p];
  uint32_t isr = UOTGHS->UOTGHS_HSTPIPISR[p];
  uint32_t imr = UOTGHS->UOTGHS_HSTPIPIMR[p];

  // STALL always wins: fail the transfer so usbh can clear-halt/retry.
  if ((isr & UOTGHS_HSTPIPISR_RXSTALLDI) && (imr & UOTGHS_HSTPIPIMR_RXSTALLDE)) {
    uhd_ack_stall(p);
    uhd_freeze_pipe(p);
    hring_log(7, p, x->total, x->queued);
    if (x->active) {
      uint8_t daddr = x->daddr, ep_addr = x->ep_addr;
      x->active = false;
      pipe_int_disable_all(p);
      hcd_event_xfer_complete(daddr, ep_addr, 0, XFER_RESULT_STALLED, true);
    }
    return;
  }

  // NAK is not enabled (see pipe_int_enable): hardware auto-retries, and a
  // stuck-NAK transfer is bounded by usbh timeouts + abort. Silently ignore
  // any stray flag.
  if ((isr & UOTGHS_HSTPIPISR_NAKEDI) && (imr & UOTGHS_HSTPIPIMR_NAKEDE)) {
    uhd_ack_nak_received(p);
  }

  // TXSTP done (SETUP sent + ACKed).
  if ((isr & UOTGHS_HSTPIPISR_TXSTPI) && (imr & UOTGHS_HSTPIPIMR_TXSTPE)) {
    uhd_ack_setup_ready(p);
    uhd_freeze_pipe(p);
    uint8_t daddr = x->daddr;
    x->active = false;
    pipe_int_disable_all(p);
    hring_log(4, p, 8, 0);
    hcd_event_xfer_complete(daddr, 0, 8, XFER_RESULT_SUCCESS, true);
    return;
  }

  // RXINI (IN data received) — check BEFORE error flags because the hardware
  // can set PERRI/CRCERRI simultaneously with RXINI when a partial read
  // succeeds. Processing the data first prevents the error handler from
  // aborting a valid transfer.
  if ((isr & UOTGHS_HSTPIPISR_RXINI) && (imr & UOTGHS_HSTPIPIMR_RXINE)) {
    // Clear any co-occurring error flags before processing data.
    if (isr & (UOTGHS_HSTPIPISR_CRCERRI | UOTGHS_HSTPIPISR_OVERFI |
               UOTGHS_HSTPIPISR_PERRI)) {
      UOTGHS->UOTGHS_HSTPIPERR[p] = 0UL;
      UOTGHS->UOTGHS_HSTPIPICR[p] = UOTGHS_HSTPIPICR_CRCERRIC |
          UOTGHS_HSTPIPICR_OVERFIC;
    }
    pipe_in_step(rhport, p);
    return;
  }

  // TXOUTI (OUT data sent).
  if ((isr & UOTGHS_HSTPIPISR_TXOUTI) && (imr & UOTGHS_HSTPIPIMR_TXOUTE)) {
    uhd_ack_out_ready(p);
    uint16_t remaining = (x->total > x->queued) ? x->total - x->queued : 0;
    if (remaining == 0) {
      uint8_t daddr = x->daddr, ep_addr = x->ep_addr;
      uint16_t done = x->queued;
      x->active = false;
      pipe_int_disable_all(p);
      uhd_freeze_pipe(p);
      hring_log(6, p, done, 0);
      hcd_event_xfer_complete(daddr, ep_addr, done, XFER_RESULT_SUCCESS, true);
      return;
    }
    uint16_t n = (remaining < x->mps) ? remaining : x->mps;
    if (n && x->buf) pipe_write_fifo(p, x->buf + x->queued, n);
    x->queued += n;
    uhd_ack_fifocon(p);
    uhd_unfreeze_pipe(p);
  }

  // Errors: retry a few times, then fail. Checked AFTER data handlers so
  // that valid RXINI/TXOUTI completions take priority over side-effect
  // flags like PERRI that can co-occur with successful data transfer.
  if (isr & (UOTGHS_HSTPIPISR_CRCERRI | UOTGHS_HSTPIPISR_OVERFI |
             UOTGHS_HSTPIPISR_PERRI)) {
    uint8_t piperr = (uint8_t) UOTGHS->UOTGHS_HSTPIPERR[p];
    UOTGHS->UOTGHS_HSTPIPERR[p] = 0UL;
    UOTGHS->UOTGHS_HSTPIPICR[p] = UOTGHS_HSTPIPICR_CRCERRIC |
        UOTGHS_HSTPIPICR_OVERFIC;
    if (x->active) {
      hring_log(9, p, piperr, x->retries);
      if (++x->retries > HCD_MAX_RETRY) {
        uint8_t daddr = x->daddr, ep_addr = x->ep_addr;
        x->active = false;
        pipe_int_disable_all(p);
        uhd_freeze_pipe(p);
        hring_log(10, p, piperr, x->retries);
        hcd_event_xfer_complete(daddr, ep_addr, 0, XFER_RESULT_FAILED, true);
        return;
      }
      hring_log(9, p, piperr, x->retries);
      uhd_unfreeze_pipe(p);
    }
  }
}

void hcd_sam3x_isr(void) {
  const uint8_t rhport = 0;
  const bool in_isr = true;
  uint32_t hstisr = UOTGHS->UOTGHS_HSTISR;
  uint32_t hstimr = UOTGHS->UOTGHS_HSTIMR;

  // Check for stuck transfers every ISR entry. This catches the case where
  // a Low-Speed device NAKs indefinitely with NAKEDI disabled — no pipe
  // interrupt fires, but the timeout must still be detected.
  hcd_check_timeouts(rhport);

  if ((hstisr & UOTGHS_HSTISR_DCONNI) && (hstimr & UOTGHS_HSTIMR_DCONNIE)) {
    uhd_ack_connection();
    UOTGHS->UOTGHS_HSTIDR = UOTGHS_HSTIDR_DCONNIEC;
    uhd_ack_disconnection();
    UOTGHS->UOTGHS_HSTIER = UOTGHS_HSTIER_DDISCIES;
    // Latch speed for EP0 MPS (LS = 8, else 64).
    if (Is_uhd_low_speed_mode()) _port_speed = TUSB_SPEED_LOW;
    else if (Is_uhd_high_speed_mode()) _port_speed = TUSB_SPEED_HIGH;
    else _port_speed = TUSB_SPEED_FULL;
    _connected = true;
    hring_log(11, 0, (uint16_t) _port_speed, 0);
    hcd_event_device_attach(rhport, in_isr);
    return;
  }

  if ((hstisr & UOTGHS_HSTISR_DDISCI) && (hstimr & UOTGHS_HSTIMR_DDISCIE)) {
    uhd_ack_disconnection();
    UOTGHS->UOTGHS_HSTIDR = UOTGHS_HSTIDR_DDISCIEC;
    uhd_ack_connection();
    UOTGHS->UOTGHS_HSTIER = UOTGHS_HSTIER_DCONNIES;
    _connected = false;
    for (int p = 0; p < PIPE_MAX; p++) {
      pipe_int_disable_all((uint8_t) p);
      if (p) { uhd_disable_pipe(p); uhd_unallocate_memory(p); }
      _pipes[p].buf = NULL; _pipes[p].total = _pipes[p].queued = 0;
      _pipes[p].active = false;
    }
    hcd_event_device_remove(rhport, in_isr);
    return;
  }

  if ((hstisr & UOTGHS_HSTISR_RSTI) && (hstimr & UOTGHS_HSTIMR_RSTIE)) {
    uhd_ack_reset_sent();
  }

  // Acknowledge SOF (1 ms heartbeat used for hcd_check_timeouts).
  if ((hstisr & UOTGHS_HSTISR_HSOFI) && (hstimr & UOTGHS_HSTIMR_HSOFIE)) {
    UOTGHS->UOTGHS_HSTICR = UOTGHS_HSTICR_HSOFIC;
  }

  for (uint8_t p = 0; p < PIPE_MAX; p++) {
    if (!(hstisr & (UOTGHS_HSTISR_PEP_0 << p))) continue;
    if (!(hstimr & (UOTGHS_HSTIMR_PEP_0 << p))) continue;
    hcd_pipe_isr(rhport, p);
  }
}

#endif
