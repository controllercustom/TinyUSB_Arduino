// dcd_sam3x.c — TinyUSB device driver for SAM3X8E UOTGHS.
//
// Register sequences follow libsam uotghs_device.c / USBCore.cpp conventions
// (DEVEPTCFG/DEVEPTCTL/DEVISR, single-bank endpoints, auto data toggle).
// SAME70's samx7x driver was NOT reusable (UDPHS is a different peripheral).
#include "tusb_option.h"

#if CFG_TUD_ENABLED && (CFG_TUSB_MCU == OPT_MCU_SAM3X)

#include "device/dcd.h"
#include "chip.h"

#define EP_MAX 10

typedef struct {
  uint8_t *buffer;
  uint16_t total_len;
  uint16_t queued_len;
  uint16_t mps;
  bool await_ack;  // final full-size packet committed; next TXINI is its ACK
  bool active; // transfer submitted, completion event still owed
} xfer_ctl_t;

static xfer_ctl_t _xfer[EP_MAX];
static uint8_t _pending_addr;
static bool _sof_enabled;

// ISR trace counters (read via dcd_sam3x_stats from task context).
typedef struct {
  uint32_t reset, setup, susp, wkup, stall;
  uint32_t in0, out0;
  uint32_t setup_bmRequestType_bRequest;
  uint32_t setup_wValue_wIndex;
  uint32_t pending_addr;
  uint32_t status_done;
  uint32_t xfer_in_submit;
  uint32_t xfer_out_submit;
  uint32_t kick_txini_set;
  uint32_t txini0_seen;
  uint32_t rxouti0_seen;
  uint32_t kick_total;
  uint32_t done_len;
  uint32_t done_ep;
  uint32_t step_calls;
  uint32_t staged_total;
  uint32_t staged_n;
  uint32_t staged_mps;
  uint32_t polled_wait;
  uint32_t polled_staged;
  uint32_t ep1_rxoute_kick;   // dcd_edpt_xfer enabled RXOUTE on EP1
  uint32_t ep1_rxouti_isr;    // ISR saw RXOUTI on EP1
  uint32_t ep1_out_complete;  // handle_ep_out completed transfer on EP1
  uint32_t ep1_no_active;     // handle_ep_out found !active on EP1
  uint32_t ep1_no_rxoute;     // handle_ep_out found RXOUTE off on EP1
  uint32_t ep1_isr_skip;      // ISR loop skipped EP1 (devisr/devimr miss)
  uint32_t ep1_cfgok_fail;    // dcd_edpt_open CFGOK never set for EP1
} dcd_sam3x_trace_t;
static volatile dcd_sam3x_trace_t _trace;

void dcd_sam3x_stats(uint32_t *reset, uint32_t *setup, uint32_t *susp,
    uint32_t *stall, uint32_t *setup_w0, uint32_t *setup_w1) {
  *reset = _trace.reset; *setup = _trace.setup; *susp = _trace.susp;
  *stall = _trace.stall; *setup_w0 = _trace.setup_bmRequestType_bRequest;
  *setup_w1 = _trace.setup_wValue_wIndex;
}

void dcd_sam3x_stats2(uint32_t *in0, uint32_t *out0, uint32_t *pending, uint32_t *status_done) {
  *in0 = _trace.in0; *out0 = _trace.out0;
  *pending = _trace.pending_addr; *status_done = _trace.status_done;
}

void dcd_sam3x_stats3(uint32_t *in_submit, uint32_t *out_submit, uint32_t *kick_txini) {
  *in_submit = _trace.xfer_in_submit; *out_submit = _trace.xfer_out_submit;
  *kick_txini = _trace.kick_txini_set;
}

void dcd_sam3x_stats4(uint32_t *txini0, uint32_t *rxouti0) {
  *txini0 = _trace.txini0_seen; *rxouti0 = _trace.rxouti0_seen;
}

void dcd_sam3x_stats5(uint32_t *kick_total, uint32_t *done_len, uint32_t *done_ep) {
  *kick_total = _trace.kick_total; *done_len = _trace.done_len; *done_ep = _trace.done_ep;
}

void dcd_sam3x_stats6(uint32_t *steps, uint32_t *staged_total, uint32_t *staged_n, uint32_t *staged_mps) {
  *steps = _trace.step_calls; *staged_total = _trace.staged_total;
  *staged_n = _trace.staged_n; *staged_mps = _trace.staged_mps;
}

void dcd_sam3x_stats7(uint32_t *kick, uint32_t *rxouti_isr, uint32_t *complete,
                       uint32_t *no_active, uint32_t *no_rxoute, uint32_t *cfgok_fail) {
  *kick = _trace.ep1_rxoute_kick; *rxouti_isr = _trace.ep1_rxouti_isr;
  *complete = _trace.ep1_out_complete; *no_active = _trace.ep1_no_active;
  *no_rxoute = _trace.ep1_no_rxoute; *cfgok_fail = _trace.ep1_cfgok_fail;
}

// stats8: live hardware register dump for EP1 diagnostic.
// Reads UOTGHS registers directly to verify actual hardware state.
// stats8: live hardware register dump for EP1 OUT (phys 1) and EP1 IN (phys 2).
void dcd_sam3x_stats8(uint32_t *cfg_out, uint32_t *isr_out,
                       uint32_t *cfg_in, uint32_t *isr_in,
                       uint32_t *devpt, uint32_t *devisr) {
  *cfg_out = UOTGHS->UOTGHS_DEVEPTCFG[1];   // phys 1 (shared: EP1 OUT/IN)
  *isr_out = UOTGHS->UOTGHS_DEVEPTISR[1];
  *cfg_in  = UOTGHS->UOTGHS_DEVEPTCFG[2];   // phys 2 (unused, for reference)
  *isr_in  = UOTGHS->UOTGHS_DEVEPTISR[2];
  *devpt   = UOTGHS->UOTGHS_DEVEPT;
  *devisr  = UOTGHS->UOTGHS_DEVISR;
}

// Mini ring log: each entry packs (code, ep, total, queued).
// code: 1=kick IN, 2=kick OUT, 3=IN complete, 4=OUT complete, 5=reset, 6=setup,
//       7=TXINI idle-cleared, 8=status_complete applied addr
#define RING_N 64
static volatile uint32_t _ring[RING_N];
static volatile uint8_t _ring_w;
static volatile uint32_t _ring_n;
static void ring_log(uint8_t code, uint8_t ep, uint16_t total, uint16_t queued) {
  _ring[_ring_w] = ((uint32_t) code << 24) | ((uint32_t) ep << 16) |
      ((uint32_t)(total & 0xFF) << 8) | (queued & 0xFF);
  _ring_w = (_ring_w + 1) % RING_N;
  _ring_n++;
}

void dcd_sam3x_ring(uint32_t *out16) {
  // chronological: oldest first
  uint8_t start = (_ring_n >= RING_N) ? _ring_w : 0;
  uint8_t count = (_ring_n >= RING_N) ? RING_N : _ring_n;
  for (int i = 0; i < count; i++) out16[i] = _ring[(start + i) % RING_N];
  for (int i = count; i < RING_N; i++) out16[i] = 0xFFFFFFFF;
}

// SAM3X UOTGHS EP direction: dynamic EPDIR switching.
//
// Each DEVEPTCFG[n] has one EPDIR bit — a physical endpoint is IN *or* OUT.
// TinyUSB uses EP 0x01 (OUT) and EP 0x81 (IN) with the same logical number.
// The hardware maps physical EPn → logical EPn (confirmed from official SAM
// core which uses physical = logical).  So both directions MUST share the
// same physical endpoint and we switch EPDIR at each transfer boundary.
//
// Mapping: physical EPn = logical EPn for all n.
static uint8_t ep_addr_to_phys(uint8_t ep_addr) {
  return tu_edpt_number(ep_addr);
}

// Switch a physical endpoint's direction.  Called from dcd_edpt_xfer before
// arming each transfer.  Disables/re-enables the endpoint to change EPDIR
// (required by SAM3X UOTGHS — datasheet §32.6.2).
static void reconfigure_epdir(uint8_t ep, uint8_t dir) {
  if (ep == 0) return;
  uint32_t cfg = UOTGHS->UOTGHS_DEVEPTCFG[ep];
  bool want_in = (dir == TUSB_DIR_IN);
  bool currently_in = (cfg & UOTGHS_DEVEPTCFG_EPDIR_IN) != 0;
  if (currently_in == want_in) return;
  ring_log(0xA, ep, cfg >> 8, dir);

  // Step 1: Disable endpoint — hardware auto-clears ALLOC per datasheet.
  UOTGHS->UOTGHS_DEVEPT &= ~(UOTGHS_DEVEPT_EPEN0 << ep);
  __DSB();

  // Step 2: Explicitly write ALLOC=0 (ensure it's cleared even if HW auto-clear fails).
  uint32_t no_alloc = cfg & ~UOTGHS_DEVEPTCFG_ALLOC;
  UOTGHS->UOTGHS_DEVEPTCFG[ep] = no_alloc;
  __DSB();

  // Step 3: Readback to confirm ALLOC=0 before changing EPDIR.
  uint32_t pre = UOTGHS->UOTGHS_DEVEPTCFG[ep];

  // Step 4: Write EPDIR (ALLOC=0, ALLOC stays 0).
  uint32_t new_cfg = (no_alloc & ~UOTGHS_DEVEPTCFG_EPDIR);
  if (want_in) new_cfg |= UOTGHS_DEVEPTCFG_EPDIR_IN;
  UOTGHS->UOTGHS_DEVEPTCFG[ep] = new_cfg;

  // Step 5: Verify EPDIR took.
  uint32_t post = UOTGHS->UOTGHS_DEVEPTCFG[ep];
  ring_log(0xB, ep, (uint8_t)(pre >> 8), (uint8_t)(post >> 8));

  // Step 6: Allocate + enable.
  UOTGHS->UOTGHS_DEVEPTCFG[ep] = new_cfg | UOTGHS_DEVEPTCFG_ALLOC;
  UOTGHS->UOTGHS_DEVEPT |= (UOTGHS_DEVEPT_EPEN0 << ep);
  __DSB();
  udd_reset_data_toggle(ep);
}

// Map wMaxPacketSize to DEVEPTCFG EPSIZE field
static uint32_t epsize_bits(uint16_t mps) {
  if (mps <= 8) return UOTGHS_DEVEPTCFG_EPSIZE_8_BYTE;
  if (mps <= 16) return UOTGHS_DEVEPTCFG_EPSIZE_16_BYTE;
  if (mps <= 32) return UOTGHS_DEVEPTCFG_EPSIZE_32_BYTE;
  if (mps <= 64) return UOTGHS_DEVEPTCFG_EPSIZE_64_BYTE;
  if (mps <= 128) return UOTGHS_DEVEPTCFG_EPSIZE_128_BYTE;
  if (mps <= 256) return UOTGHS_DEVEPTCFG_EPSIZE_256_BYTE;
  if (mps <= 512) return UOTGHS_DEVEPTCFG_EPSIZE_512_BYTE;
  return UOTGHS_DEVEPTCFG_EPSIZE_1024_BYTE;
}

static uint16_t epsize_bytes(uint32_t cfg) {
  switch (cfg & UOTGHS_DEVEPTCFG_EPSIZE_Msk) {
    case UOTGHS_DEVEPTCFG_EPSIZE_8_BYTE: return 8;
    case UOTGHS_DEVEPTCFG_EPSIZE_16_BYTE: return 16;
    case UOTGHS_DEVEPTCFG_EPSIZE_32_BYTE: return 32;
    case UOTGHS_DEVEPTCFG_EPSIZE_64_BYTE: return 64;
    case UOTGHS_DEVEPTCFG_EPSIZE_128_BYTE: return 128;
    case UOTGHS_DEVEPTCFG_EPSIZE_256_BYTE: return 256;
    case UOTGHS_DEVEPTCFG_EPSIZE_512_BYTE: return 512;
    default: return 1024;
  }
}

static void ep_write_fifo(uint8_t ep, uint8_t const *src, uint16_t len, uint16_t off) {
  volatile uint8_t *dst = (volatile uint8_t *) &udd_get_endpoint_fifo_access8(ep);
  for (uint16_t i = 0; i < len; i++) dst[off + i] = src[i];
}

static void ep_read_fifo(uint8_t ep, uint8_t *dst, uint16_t len, uint16_t off) {
  volatile uint8_t *src = (volatile uint8_t *) &udd_get_endpoint_fifo_access8(ep);
  for (uint16_t i = 0; i < len; i++) dst[i] = src[off + i];
}

// Advance the IN transfer by one bank. Called only when TXINI is set (bank
// free). Commits at most one packet. Returns true when the transfer is fully
// done and its completion event must be fired by the caller.
//
// Contract (matches usbd's xact chaining): send EXACTLY total_len bytes —
// never auto-append a ZLP. usbd delivers ZLPs as zero-length transfers when
// the host needs termination, and chains multi-packet transfers itself.
// Completion rule:
//   - empty transfer (status ZLP): commit, complete on the ACK (next TXINI).
//     The ACK wait is required e.g. for SET_ADDRESS, whose address must only
//     apply after the host took the ZLP.
//   - short final packet: the host ends the transfer on it, so NO further IN
//     (and no TXINI) will come — complete AT COMMIT. Safe: usbd's next step
//     (status submit or next xact) is host-driven or TXINI-guarded.
//   - full-size final packet: complete on the ACK (next TXINI). The host
//     always INs again here (it can't know we're done), so the ACK comes.
static bool in_xfer_step(uint8_t ep) {  if (!(UOTGHS->UOTGHS_DEVEPTISR[ep] & UOTGHS_DEVEPTISR_TXINI)) return false;
  xfer_ctl_t *x = &_xfer[ep];
  _trace.step_calls++;
  if (x->await_ack) {
    x->await_ack = false;
    ring_log(9, ep, x->total_len, x->queued_len);
    return true;
  }
  uint16_t remaining = (x->total_len > x->queued_len) ? x->total_len - x->queued_len : 0;
  if (remaining == 0) {
    // Empty (status ZLP) transfer: commit one ZLP, complete on its ACK.
    // Commit with TXINIC only on EP0 (libsam USBCore UDD_ClearIN); other
    // endpoints take TXINIC+FIFOCONC like UDD_Send().
    UOTGHS->UOTGHS_DEVEPTICR[ep] = UOTGHS_DEVEPTICR_TXINIC;
    if (ep != 0) UOTGHS->UOTGHS_DEVEPTIDR[ep] = UOTGHS_DEVEPTIDR_FIFOCONC;
    x->await_ack = true;
    return false;
  }
  uint16_t n = (remaining < x->mps) ? remaining : x->mps;
  if (n && x->buffer) ep_write_fifo(ep, x->buffer + x->queued_len, n, 0);
  x->queued_len += n;
  _trace.staged_total = x->total_len; _trace.staged_n = n; _trace.staged_mps = x->mps;
  // Commit packet.
  UOTGHS->UOTGHS_DEVEPTICR[ep] = UOTGHS_DEVEPTICR_TXINIC;
  if (ep != 0) UOTGHS->UOTGHS_DEVEPTIDR[ep] = UOTGHS_DEVEPTIDR_FIFOCONC;
  if (n < x->mps) {
    // Short final packet: host terminates here, no more TXINI — done now.
    ring_log(9, ep, x->total_len, x->queued_len);
    return true;
  }
  if (x->queued_len >= x->total_len) {
    // Full-size final packet: host INs again, complete on that ACK.
    x->await_ack = true;
  }
  return false;
}

//--------------------------------------------------------------------+
// Device API
//--------------------------------------------------------------------+

bool dcd_init(uint8_t rhport, const tusb_rhport_init_t *rh_init) {
  (void) rhport; (void) rh_init;
  for (int i = 0; i < EP_MAX; i++) {
    _xfer[i].buffer = NULL;
    _xfer[i].total_len = _xfer[i].queued_len = 0;
    _xfer[i].mps = 64;
    _xfer[i].await_ack = _xfer[i].active = false;
  }
  _pending_addr = 0;
  _sof_enabled = false;
  dcd_connect(rhport);
  return true;
}

void dcd_int_enable(uint8_t rhport) {
  (void) rhport;
  NVIC_EnableIRQ(UOTGHS_IRQn);
}

void dcd_int_disable(uint8_t rhport) {
  (void) rhport;
  NVIC_DisableIRQ(UOTGHS_IRQn);
}

void dcd_set_address(uint8_t rhport, uint8_t dev_addr) {
  // Address is applied after the status stage (dcd_edpt0_status_complete).
  _pending_addr = dev_addr;
  dcd_edpt_xfer(rhport, tu_edpt_addr(0, TUSB_DIR_IN), NULL, 0, false);
}

void dcd_remote_wakeup(uint8_t rhport) {
  (void) rhport;
  UOTGHS->UOTGHS_DEVCTRL |= UOTGHS_DEVCTRL_RMWKUP;
  while (UOTGHS->UOTGHS_DEVCTRL & UOTGHS_DEVCTRL_RMWKUP) {}
}

void dcd_connect(uint8_t rhport) {
  (void) rhport;
  udd_attach_device();
  udd_enable_reset_interrupt();
  // NOTE: wake-up/suspend interrupts stay DISABLED on purpose. The WAKEUP
  // status bit sticks on this silicon: with WAKEUPE on, every ISR visit
  // re-queued a RESUME event and flooded the 16-deep queue (assert storm,
  // dead enumeration). Suspend/resume then simply isn't tracked — the
  // hardware recovers the clock on bus activity by itself, and EORST still
  // re-syncs usbd after a real reset. See also the IMR-gated SUSP/WAKEUP
  // checks in dcd_sam3x_isr (belt and suspenders).
}

void dcd_disconnect(uint8_t rhport) {
  (void) rhport;
  udd_detach_device();
}

void dcd_sof_enable(uint8_t rhport, bool en) {
  (void) rhport;
  _sof_enabled = en;
  if (en) UOTGHS->UOTGHS_DEVIER = UOTGHS_DEVIER_SOFES;
  else UOTGHS->UOTGHS_DEVIDR = UOTGHS_DEVIDR_SOFEC;
}

bool dcd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const *desc_ep) {
  (void) rhport;
  uint8_t ep_addr = desc_ep->bEndpointAddress;
  uint8_t ep = ep_addr_to_phys(ep_addr);
  if (ep >= EP_MAX) return false;
  uint16_t mps = tu_edpt_packet_size(desc_ep);
  // If this physical endpoint is already configured (e.g. EP 0x81 mapping to
  // the same physical EP as EP 0x01), skip hardware setup.  DEVEPTCFG can
  // only be written when EPEN=0; writing while EPEN=1 is undefined.
  // reconfigure_epdir() in dcd_edpt_xfer handles direction switching.
  if (ep != 0 && (UOTGHS->UOTGHS_DEVEPT & (UOTGHS_DEVEPT_EPEN0 << ep))) {
    _xfer[ep].mps = mps;
    return true;
  }
  uint32_t cfg = epsize_bits(mps) | UOTGHS_DEVEPTCFG_ALLOC;
  if (ep == 0) {
    cfg |= UOTGHS_DEVEPTCFG_EPTYPE_CTRL | UOTGHS_DEVEPTCFG_EPBK_1_BANK;
  } else {
    if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN) cfg |= UOTGHS_DEVEPTCFG_EPDIR_IN;
    switch (desc_ep->bmAttributes.xfer) {
      // NOTE (ISO): NBTRANS defaults to 0_TRANS ("reserved to endpoints
      // without high-bandwidth isochronous capability") — an ISO endpoint
      // opened without NBTRANS_1_TRANS never answers IN tokens (observed:
      // host ISO INs in, nothing out, TXINI never fires). Bulk/interrupt/
      // control intentionally keep NBTRANS=0 (proven paths, field reserved
      // for non-ISO).
      case TUSB_XFER_ISOCHRONOUS: cfg |= UOTGHS_DEVEPTCFG_EPTYPE_ISO | UOTGHS_DEVEPTCFG_NBTRANS_1_TRANS; break;
      case TUSB_XFER_BULK: cfg |= UOTGHS_DEVEPTCFG_EPTYPE_BLK; break;
      default: cfg |= UOTGHS_DEVEPTCFG_EPTYPE_INTRPT; break;
    }
    cfg |= UOTGHS_DEVEPTCFG_EPBK_1_BANK; // single-bank: simplest correct FIFO model
  }
  UOTGHS->UOTGHS_DEVEPTCFG[ep] = cfg;
  udd_enable_endpoint(ep);
  // Wait briefly for the DPRAM allocation (CFGOK). Observed: on first config
  // of a non-zero EP this can outlast a tight 10k loop; the old code returned
  // false then and usbd skipped the endpoint (dead bulk) while CFGOK set fine
  // shortly after. A long spin here is worse (task blocked, event queue
  // floods, TU_ASSERT breakpoint kills the firmware), so wait briefly and
  // ALWAYS proceed — transfers are submitted milliseconds later, by which
  // time the hardware has caught up (verified live: CFGOK sets).
  for (volatile uint32_t i = 0; i < 10000; i++) {
    if (UOTGHS->UOTGHS_DEVEPTISR[ep] & UOTGHS_DEVEPTISR_CFGOK) break;
  }
  _xfer[ep].mps = epsize_bytes(cfg);
  if (_xfer[ep].mps > mps) _xfer[ep].mps = mps;
  udd_reset_data_toggle(ep);
  UOTGHS->UOTGHS_DEVIER = (UOTGHS_DEVIER_PEP_0 << ep);
  if (ep == 1 && !(UOTGHS->UOTGHS_DEVEPTISR[ep] & UOTGHS_DEVEPTISR_CFGOK)) {
    _trace.ep1_cfgok_fail++;
  }
  // NOTE: neither TXINE nor RXOUTE is enabled here. A permanently-enabled
  // TXINE makes every spontaneous bank-free TXINI interrupt us, and clearing
  // TXINI with an empty bank commits a SPURIOUS ZLP (observed). Symmetrically,
  // a permanently-enabled RXOUTE lets the ISR drain (and silently DISCARD) a
  // packet that arrives before usbd resubmits — back-to-back CBWs then
  // deadlock (host waits CSW, device waits CBW). Both enables are
  // transfer-gated: kick enables, completion disables; the kick drains inline
  // anything already pending. Stale enables self-heal (ISR disables on
  // !active without touching flags).
  if (ep == 0) {
    udd_enable_setup_received_interrupt(0);
  }
  return true;
}

void dcd_edpt_close_all(uint8_t rhport) {
  (void) rhport;
  for (int ep = 1; ep < EP_MAX; ep++) {
    UOTGHS->UOTGHS_DEVEPTCFG[ep] = 0;
    udd_disable_endpoint(ep);
  }
}

void dcd_edpt_close(uint8_t rhport, uint8_t ep_addr) {
  (void) rhport;
  uint8_t ep = ep_addr_to_phys(ep_addr);
  if (ep == 0 || ep >= EP_MAX) return;
  UOTGHS->UOTGHS_DEVEPTCFG[ep] = 0;
  udd_disable_endpoint(ep);
}

// Forward: OUT kick drains inline anything already pending.
static void handle_ep_out(uint8_t rhport, uint8_t ep, bool in_isr);

bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t *buffer, uint16_t total_bytes, bool is_isr) {
  uint8_t ep = ep_addr_to_phys(ep_addr);
  if (ep >= EP_MAX) return false;
  xfer_ctl_t *x = &_xfer[ep];
  x->buffer = buffer;
  x->total_len = total_bytes;
  x->queued_len = 0;
  x->await_ack = false;
  x->active = true;
  if (tu_edpt_dir(ep_addr) == TUSB_DIR_IN) {
    _trace.xfer_in_submit++;
    _trace.kick_total = total_bytes;
    reconfigure_epdir(ep, TUSB_DIR_IN);
    _trace.kick_txini_set = (UOTGHS->UOTGHS_DEVEPTISR[ep] & UOTGHS_DEVEPTISR_TXINI) ? 1 : 0;
    ring_log(1, ep, total_bytes, _trace.kick_txini_set);
    // RACE GUARANTEE: a TXINI IRQ asserted at kick time (bank free + TXINE
    // about to be enabled) can preempt in_xfer_step between its FIFO write
    // and commit, staging the NEXT packet over the first (silent 64 B skip
    // → host hangs awaiting bytes usbd already reported). So keep TXINE
    // DISABLED across the kick step (no TXINI IRQ can preempt it), then
    // enable for ACKs/continuation. (Observed: multi-packet bulk-IN reads
    // wedging the host after minutes of clean traffic.)
    UOTGHS->UOTGHS_DEVEPTIDR[ep] = UOTGHS_DEVEPTIDR_TXINEC;
    if (in_xfer_step(ep)) {
      // Short packet committed at kick: per the in_xfer_step contract this
      // transfer is done AT COMMIT (bulk host sends no more IN; ISO bytes
      // are already staged). Complete NOW — swallowing this (old behavior)
      // left a stale active state that committed a SPURIOUS ZLP on the next
      // TXINI (observed: every MSC read failing DID_ERROR+reset right after
      // the 13-byte CSW). TXINE stays disabled (never enabled above), so the
      // idle bank-free TXINI can't commit anything.
      uint16_t done = x->queued_len;
      x->buffer = NULL; x->total_len = x->queued_len = 0;
      x->active = false;
      ring_log(3, ep, done, 0);
      dcd_event_xfer_complete(rhport, ep_addr, done, XFER_RESULT_SUCCESS, is_isr);
    } else {
      UOTGHS->UOTGHS_DEVEPTIER[ep] = UOTGHS_DEVEPTIER_TXINES;
    }
  } else {
    _trace.xfer_out_submit++;
    ring_log(2, ep, total_bytes, 0);
    reconfigure_epdir(ep, TUSB_DIR_OUT);
    // OUT: enable RXOUTE for this transfer, then drain inline anything that
    // beat the task here (back-to-back CBW arriving before usbd resubmits).
    // The drain is idempotent via the active flag: if the ISR already took
    // it, RXOUTI is clear and this is a no-op; if the ISR preempts mid-kick
    // it either drains with fresh state (fine) or finds active already true
    // with nothing new (no-op). Either way each packet completes exactly once.
    UOTGHS->UOTGHS_DEVEPTIER[ep] = UOTGHS_DEVEPTIER_RXOUTES;
    if (ep == 1) _trace.ep1_rxoute_kick++;
    if (ep == 0) {
      // EP0: the just-enabled RXOUTE turns an already-pending RXOUTI level
      // into an NVIC IRQ, so the ISR drains on task return. Delivery
      // guaranteed, no inline work needed.
    } else if ((UOTGHS->UOTGHS_DEVEPTISR[ep] & UOTGHS_DEVEPTISR_RXOUTI) &&
               (UOTGHS->UOTGHS_DEVEPTIMR[ep] & UOTGHS_DEVEPTIMR_RXOUTE)) {
      handle_ep_out(rhport, ep, false);
    }
  }
  return true;
}

bool dcd_edpt_iso_alloc(uint8_t rhport, uint8_t ep_addr, uint16_t largest_packet_size) {
  (void) rhport; (void) ep_addr; (void) largest_packet_size;
  return true; // single-bank DPRAM needs no pre-allocation
}

bool dcd_edpt_iso_activate(uint8_t rhport, tusb_desc_endpoint_t const *desc_ep) {
  return dcd_edpt_open(rhport, desc_ep);
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr) {
  (void) rhport;
  udd_enable_stall_handshake(ep_addr_to_phys(ep_addr));
}

void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr) {
  (void) rhport;
  uint8_t ep = ep_addr_to_phys(ep_addr);
  udd_disable_stall_handshake(ep);
  udd_reset_data_toggle(ep);
  _xfer[ep].queued_len = _xfer[ep].total_len = 0;
}

void dcd_edpt0_status_complete(uint8_t rhport, tusb_control_request_t const *request) {
  (void) rhport; (void) request;
  _trace.status_done++;
  if (_pending_addr) {
    udd_configure_address(_pending_addr);
    udd_enable_address();
    _trace.pending_addr = _pending_addr;
    _pending_addr = 0;
    // udd_configure_address() rewrites DEVCTRL wholesale, wiping the
    // SPDCONF_FORCED_FS set at init (Klipper-style FS lock). Re-apply it —
    // else the controller falls back to SPDCONF_NORMAL after SET_ADDRESS.
    UOTGHS->UOTGHS_DEVCTRL |= UOTGHS_DEVCTRL_SPDCONF_FORCED_FS;
  }
}

//--------------------------------------------------------------------+
// ISR
//--------------------------------------------------------------------+

static void handle_reset(uint8_t rhport, bool in_isr) {
  _trace.reset++;
  ring_log(5, 0, 0, 0);
  udd_configure_address(0);
  udd_enable_address();
  // Re-init EP0: 64-byte control, single bank
  UOTGHS->UOTGHS_DEVEPTCFG[0] = UOTGHS_DEVEPTCFG_EPSIZE_64_BYTE |
      UOTGHS_DEVEPTCFG_EPTYPE_CTRL | UOTGHS_DEVEPTCFG_EPBK_1_BANK |
      UOTGHS_DEVEPTCFG_ALLOC;
  udd_enable_endpoint(0);
  _xfer[0].mps = 64;
  _xfer[0].buffer = NULL;
  _xfer[0].total_len = _xfer[0].queued_len = 0;
  _xfer[0].active = false;
  // Tear down all non-zero endpoints: the stack re-opens them after reset.
  for (int ep = 1; ep < EP_MAX; ep++) {
    UOTGHS->UOTGHS_DEVEPTCFG[ep] = 0;
    UOTGHS->UOTGHS_DEVEPT &= ~(UOTGHS_DEVEPT_EPEN0 << ep);
    _xfer[ep].buffer = NULL;
    _xfer[ep].total_len = _xfer[ep].queued_len = 0;
    _xfer[ep].active = false;
  }
  _pending_addr = 0;
  UOTGHS->UOTGHS_DEVIER = UOTGHS_DEVIER_PEP_0;
  udd_enable_setup_received_interrupt(0);
  // (TXINE/RXOUTE stay disabled until a transfer is kicked — see dcd_edpt_open.)
  udd_ack_reset();
  dcd_event_bus_reset(rhport, TUSB_SPEED_FULL, in_isr);
}

static void handle_ep0(uint8_t rhport, bool in_isr) {
  uint32_t episr = UOTGHS->UOTGHS_DEVEPTISR[0];

  // SETUP packet has priority
  if (episr & UOTGHS_DEVEPTISR_RXSTPI) {
    uint8_t setup[8];
    ep_read_fifo(0, setup, 8, 0);
    udd_ack_setup_received(0);
    _trace.setup++;
    ring_log(6, 0, ((uint16_t) setup[3] << 8) | setup[2], ((uint16_t) setup[1] << 8) | setup[0]);
    _trace.setup_bmRequestType_bRequest = setup[0] | ((uint32_t) setup[1] << 8);
    _trace.setup_wValue_wIndex = setup[2] | ((uint32_t) setup[3] << 8) |
        ((uint32_t) setup[4] << 16) | ((uint32_t) setup[5] << 24);
    // A new SETUP aborts any in-progress EP0 transfer (usbd supersedes its
    // control state the same way). Kill it SILENTLY — no completion event —
    // or a stale active flag would fire a phantom zero-length completion.
    _xfer[0].buffer = NULL;
    _xfer[0].total_len = _xfer[0].queued_len = 0;
    _xfer[0].await_ack = _xfer[0].active = false;
    // USB spec §8.5.3.4: a SETUP token always clears the STALL condition.
    // The SAM3X hardware is supposed to do this automatically, but clear
    // STALLRQ explicitly as a safety measure so the subsequent status ZLP
    // (for SET_ADDRESS etc.) isn't blocked by a stale stall.
    UOTGHS->UOTGHS_DEVEPTIDR[0] = UOTGHS_DEVEPTIDR_STALLRQC;
    dcd_event_setup_received(rhport, setup, in_isr);
    return;
  }

  uint32_t epimr = UOTGHS->UOTGHS_DEVEPTIMR[0];
  xfer_ctl_t *x = &_xfer[0];

  // IN packet sent
  if ((episr & UOTGHS_DEVEPTISR_TXINI) && (epimr & UOTGHS_DEVEPTIMR_TXINE)) {
    _trace.txini0_seen++;
    if (x->active || x->total_len) {
      ring_log(10, 0, x->total_len, (x->active ? 0xA0 : 0x00) | (x->await_ack ? 0x0A : 0x00));
    }
    if (x->active) {
      if (in_xfer_step(0)) {
        uint16_t done = x->queued_len;
        x->buffer = NULL; x->total_len = x->queued_len = 0;
        x->active = false;
        // Transfer over: stop TXINE so idle bank-free TXINI never bothers us
        // (clearing it would commit a spurious ZLP).
        UOTGHS->UOTGHS_DEVEPTIDR[0] = UOTGHS_DEVEPTIDR_TXINEC;
        _trace.in0++;
        _trace.done_len = done; _trace.done_ep = 0x80;
        ring_log(3, 0, done, 0);
        dcd_event_xfer_complete(rhport, tu_edpt_addr(0, TUSB_DIR_IN), done,
            XFER_RESULT_SUCCESS, in_isr);
      }
    } else {
      // No active transfer: a stale TXINE (e.g. pre-kick). Disable it and
      // touch NOTHING — clearing TXINI with an empty bank would send a
      // spurious ZLP to the host.
      UOTGHS->UOTGHS_DEVEPTIDR[0] = UOTGHS_DEVEPTIDR_TXINEC;
    }
  }

  // OUT packet received (control OUT data or status)
  if ((episr & UOTGHS_DEVEPTISR_RXOUTI) && (epimr & UOTGHS_DEVEPTIMR_RXOUTE)) {
    _trace.rxouti0_seen++;
    if (!x->active) {
      // No active transfer (packet beat the kick, or stale): leave it BANKED
      // (do NOT clear/release!) and stop the interrupt. The kick enables
      // RXOUTE and drains inline; a stale enable self-heals here. Releasing
      // now would silently DISCARD the packet (observed: back-to-back CBWs
      // deadlocking bulk).
      UOTGHS->UOTGHS_DEVEPTIDR[0] = UOTGHS_DEVEPTIDR_RXOUTEC;
      return;
    }
    uint16_t byct = (UOTGHS->UOTGHS_DEVEPTISR[0] & UOTGHS_DEVEPTISR_BYCT_Msk) >>
        UOTGHS_DEVEPTISR_BYCT_Pos;
    if (x->buffer && x->total_len) {
      uint16_t remaining = x->total_len - x->queued_len;
      uint16_t n = (byct < remaining) ? byct : remaining;
      ep_read_fifo(0, x->buffer + x->queued_len, n, 0);
      x->queued_len += n;
    }
    UOTGHS->UOTGHS_DEVEPTICR[0] = UOTGHS_DEVEPTICR_RXOUTIC;
    UOTGHS->UOTGHS_DEVEPTIDR[0] = UOTGHS_DEVEPTIDR_FIFOCONC;
    if (!x->buffer || x->queued_len >= x->total_len || byct < x->mps) {
      uint16_t done = x->queued_len;
      x->buffer = NULL; x->total_len = x->queued_len = 0;
      x->active = false;
      UOTGHS->UOTGHS_DEVEPTIDR[0] = UOTGHS_DEVEPTIDR_RXOUTEC;
      _trace.out0++;
      dcd_event_xfer_complete(rhport, tu_edpt_addr(0, TUSB_DIR_OUT), done,
          XFER_RESULT_SUCCESS, in_isr);
    }
  }
}

static void handle_ep_in(uint8_t rhport, uint8_t ep, bool in_isr) {
  xfer_ctl_t *x = &_xfer[ep];
  if (!(UOTGHS->UOTGHS_DEVEPTIMR[ep] & UOTGHS_DEVEPTIMR_TXINE)) return;
  if (!x->active) {
    // Stale TXINE: disable, touch nothing (spurious-ZLP guard, self-heals).
    UOTGHS->UOTGHS_DEVEPTIDR[ep] = UOTGHS_DEVEPTIDR_TXINEC;
    return;
  }
  if (in_xfer_step(ep)) {
    uint16_t done = x->queued_len;
    x->buffer = NULL; x->total_len = x->queued_len = 0;
    x->active = false;
    UOTGHS->UOTGHS_DEVEPTIDR[ep] = UOTGHS_DEVEPTIDR_TXINEC;
    dcd_event_xfer_complete(rhport, tu_edpt_addr(ep, TUSB_DIR_IN), done,
        XFER_RESULT_SUCCESS, in_isr);
  }
}

static void handle_ep_out(uint8_t rhport, uint8_t ep, bool in_isr) {
  xfer_ctl_t *x = &_xfer[ep];
  if (!(UOTGHS->UOTGHS_DEVEPTIMR[ep] & UOTGHS_DEVEPTIMR_RXOUTE)) {
    if (ep == 1) _trace.ep1_no_rxoute++;
    return;
  }
  if (!x->active) {
    if (ep == 1) _trace.ep1_no_active++;
    // No active transfer: leave the packet BANKED (do NOT clear/release —
    // that would silently discard a CBW that beat the kick) and stop the
    // interrupt. The kick re-enables RXOUTE and drains inline.
    UOTGHS->UOTGHS_DEVEPTIDR[ep] = UOTGHS_DEVEPTIDR_RXOUTEC;
    return;
  }
  uint16_t byct = (UOTGHS->UOTGHS_DEVEPTISR[ep] & UOTGHS_DEVEPTISR_BYCT_Msk) >>
      UOTGHS_DEVEPTISR_BYCT_Pos;
  if (x->buffer) {
    uint16_t remaining = (x->total_len > x->queued_len) ? x->total_len - x->queued_len : 0;
    uint16_t n = (byct < remaining) ? byct : remaining;
    if (n) ep_read_fifo(ep, x->buffer + x->queued_len, n, 0);
    x->queued_len += n;
  }
  UOTGHS->UOTGHS_DEVEPTICR[ep] = UOTGHS_DEVEPTICR_RXOUTIC;
  UOTGHS->UOTGHS_DEVEPTIDR[ep] = UOTGHS_DEVEPTIDR_FIFOCONC;
  if (x->active && (!x->buffer || x->queued_len >= x->total_len || byct < x->mps)) {
    uint16_t done = x->queued_len;
    if (ep == 1) _trace.ep1_out_complete++;
    if (x->buffer) { x->buffer = NULL; x->total_len = x->queued_len = 0; }
    x->active = false;
    UOTGHS->UOTGHS_DEVEPTIDR[ep] = UOTGHS_DEVEPTIDR_RXOUTEC;
    dcd_event_xfer_complete(rhport, tu_edpt_addr(ep, TUSB_DIR_OUT), done,
        XFER_RESULT_SUCCESS, in_isr);
  }
}

void dcd_sam3x_isr(void) {
  const uint8_t rhport = 0;
  const bool in_isr = true;
  uint32_t devisr = UOTGHS->UOTGHS_DEVISR;
  uint32_t devimr = UOTGHS->UOTGHS_DEVIMR;

  if ((devisr & UOTGHS_DEVISR_EORST) && (devimr & UOTGHS_DEVIMR_EORSTE)) {
    handle_reset(rhport, in_isr);
    return;
  }
  if ((devisr & UOTGHS_DEVISR_SUSP) && (devimr & UOTGHS_DEVIMR_SUSPE)) {
    UOTGHS->UOTGHS_DEVICR = UOTGHS_DEVICR_SUSPC;
    _trace.susp++;
    dcd_event_bus_signal(rhport, DCD_EVENT_SUSPEND, in_isr);
  }
  if ((devisr & UOTGHS_DEVISR_WAKEUP) && (devimr & UOTGHS_DEVIMR_WAKEUPE)) {
    UOTGHS->UOTGHS_DEVICR = UOTGHS_DEVICR_WAKEUPC;
    _trace.wkup++;
    dcd_event_bus_signal(rhport, DCD_EVENT_RESUME, in_isr);
  }
  if ((devisr & UOTGHS_DEVISR_SOF) && _sof_enabled) {
    UOTGHS->UOTGHS_DEVICR = UOTGHS_DEVICR_SOFC;
    dcd_event_sof(rhport, udd_frame_number(), in_isr);
  }

  for (uint8_t ep = 0; ep < EP_MAX; ep++) {
    if (!(devisr & (UOTGHS_DEVISR_PEP_0 << ep))) continue;
    if (!(devimr & (UOTGHS_DEVIMR_PEP_0 << ep))) continue;
    if (ep == 0) handle_ep0(rhport, in_isr);
    else {
      uint32_t episr = UOTGHS->UOTGHS_DEVEPTISR[ep];
      if (ep == 1 && (episr & UOTGHS_DEVEPTISR_RXOUTI)) _trace.ep1_rxouti_isr++;
      if (episr & UOTGHS_DEVEPTISR_TXINI) handle_ep_in(rhport, ep, in_isr);
      if (episr & UOTGHS_DEVEPTISR_RXOUTI) handle_ep_out(rhport, ep, in_isr);
      if (episr & UOTGHS_DEVEPTISR_STALLEDI) {
        UOTGHS->UOTGHS_DEVEPTICR[ep] = UOTGHS_DEVEPTICR_STALLEDIC;
        _trace.stall++;
      }
      if (episr & UOTGHS_DEVEPTISR_RXSTPI) {
        // STALL/setup on non-zero EP: ack to avoid sticky interrupt.
        UOTGHS->UOTGHS_DEVEPTICR[ep] = UOTGHS_DEVEPTICR_RXSTPIC;
      }
    }
  }
}

#endif
