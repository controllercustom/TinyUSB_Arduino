// Host_MSC.ino — Reads raw sectors from a USB flash drive.
//
// Plug a USB flash drive into the host port.  The sketch enumerates
// it via TinyUSB MSC, reads the first sector (MBR or FAT boot sector),
// and prints a hex dump.  It also demonstrates how to perform
// asynchronous block read/write operations.
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC

#include "ArduinoTinyUSB.h"

// ── State ────────────────────────────────────────────────────────────

static bool     msc_ready = false;
static uint8_t  msc_dev_addr = 0;
static uint32_t block_count = 0;
static uint32_t block_size  = 0;
// One SCSI command at a time: the MSC stage machine tracks a single
// in-flight command. Firing read10 while one is pending overwrites its
// CBW/buffer/callback and corrupts the transfer (observed: CBW routed
// to ghost EP 0x12). Guarded here AND by TU_VERIFY(IDLE) in the stack.
static bool     msc_busy = false;

// Sector buffer — must be accessible by USB/DMA, cache-line aligned
CFG_TUH_MEM_ALIGN static uint8_t sector_buf[512];

// ── Helpers ──────────────────────────────────────────────────────────

static void hex_dump(const uint8_t *data, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    if (data[i] < 0x10) ARDUINO_TINYUSB_CONSOLE.write('0');
    ARDUINO_TINYUSB_CONSOLE.print(data[i], HEX);
    if ((i & 0x0F) == 0x0F) ARDUINO_TINYUSB_CONSOLE.println();
    else ARDUINO_TINYUSB_CONSOLE.write(' ');
  }
}

// ── Completion callback for asynchronous read ────────────────────────

static bool read_complete_cb(uint8_t dev_addr,
                             const tuh_msc_complete_data_t *cb_data) {
  (void)dev_addr;
  msc_csw_t const *csw = cb_data->csw;

  if (csw->status == 0) {
    ARDUINO_TINYUSB_CONSOLE.println(F("\n--- Sector 0 (hex dump) ---"));
    hex_dump(sector_buf, 512);
  } else {
    ARDUINO_TINYUSB_CONSOLE.println(F("Read failed (CSW status != 0)"));
  }

  msc_busy = false;
  return true;
}

// ── TinyUSB Host MSC callbacks ───────────────────────────────────────

extern "C" void tuh_msc_mount_cb(uint8_t dev_addr) {
  msc_dev_addr = dev_addr;
  uint8_t lun = 0;

  block_count = tuh_msc_get_block_count(dev_addr, lun);
  block_size  = tuh_msc_get_block_size(dev_addr, lun);

  ARDUINO_TINYUSB_CONSOLE.print(F("\n── MSC Mounted ──\n"));
  ARDUINO_TINYUSB_CONSOLE.print(F("  addr="));       ARDUINO_TINYUSB_CONSOLE.print(dev_addr);
  ARDUINO_TINYUSB_CONSOLE.print(F("  blocks="));     ARDUINO_TINYUSB_CONSOLE.print(block_count);
  ARDUINO_TINYUSB_CONSOLE.print(F("  block_size=")); ARDUINO_TINYUSB_CONSOLE.print(block_size);
  ARDUINO_TINYUSB_CONSOLE.print(F("  capacity="));   ARDUINO_TINYUSB_CONSOLE.print(block_count);
  ARDUINO_TINYUSB_CONSOLE.println(F(" bytes"));
  ARDUINO_TINYUSB_CONSOLE.println(F(" bytes"));

  msc_ready = true;

  // Read sector 0 (MBR or FAT boot sector) using asynchronous API
  ARDUINO_TINYUSB_CONSOLE.println(F("  Reading sector 0..."));
  tuh_msc_read10(dev_addr, lun, sector_buf, 0, 1, read_complete_cb, 0);
}

extern "C" void tuh_msc_umount_cb(uint8_t dev_addr) {
  ARDUINO_TINYUSB_CONSOLE.print(F("\n── MSC Removed ── addr="));
  ARDUINO_TINYUSB_CONSOLE.println(dev_addr);
  msc_ready    = false;
  msc_dev_addr = 0;
  block_count  = 0;
  block_size   = 0;
}

// ── Arduino setup / loop ────────────────────────────────────────────

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(115200);
  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { /* wait for Serial */ }

  ARDUINO_TINYUSB_CONSOLE.println(F("\n=== TinyUSB Host MSC ==="));

  // Initialise TinyUSB host stack
  tuh_arduino_init();

  ARDUINO_TINYUSB_CONSOLE.println(F("Waiting for USB flash drive...\n"));
}

void loop() {
  // Run the TinyUSB host task — drives enumeration & callbacks
  tuh_arduino_poll();

  // Example: read a different sector every 5 seconds while drive is present.
  // Serialized via msc_busy: never fire while a command is in flight.
  static uint32_t last_read = 0;
  static uint32_t next_lba  = 1;  // skip sector 0 (already read on mount)

  if (msc_ready && !msc_busy && millis() - last_read >= 5000) {
    last_read = millis();

    if (next_lba < block_count) {
      ARDUINO_TINYUSB_CONSOLE.print(F("Reading sector "));
      ARDUINO_TINYUSB_CONSOLE.print(next_lba);
      ARDUINO_TINYUSB_CONSOLE.println(F("..."));

      if (tuh_msc_read10(msc_dev_addr, 0, sector_buf, next_lba, 1,
                         read_complete_cb, 0)) {
        msc_busy = true;
        next_lba++;
      }
    }
  }
}
