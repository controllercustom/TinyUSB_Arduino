// Device_DFU_RT.ino — DFU Runtime detach example.
//
// Exposes a DFU Runtime interface so the host can request a detach
// (reboot into DFU bootloader) via dfu-util or serial command.
//
// Works on ALL boards:
//   Due/Zero/GIGA: Serial = native USB CDC

#include <ArduinoTinyUSB.h>

static volatile bool dfu_detach_requested = false;

// Called by TinyUSB when the host sends DFU_DETACH.
extern "C" void tud_dfu_runtime_reboot_to_dfu_cb(void) {
  dfu_detach_requested = true;
}

void setup() {
  ARDUINO_TINYUSB_CONSOLE.begin(ARDUINO_TINYUSB_BAUD);

  // Init TinyUSB FIRST (before any console wait): on GIGA this releases
  // Mbed's CDC before the host can enumerate it.
  tud_arduino_init();

  while (!ARDUINO_TINYUSB_CONSOLE && millis() < 2000) { delay(1); }

  ARDUINO_TINYUSB_CONSOLE.println("[DFU_RT] Arduino-TinyUSB DFU Runtime");
  ARDUINO_TINYUSB_CONSOLE.println("[DFU_RT] Send 'd' or press BTN to detach into DFU bootloader.");

  ARDUINO_TINYUSB_CONSOLE.println("[DFU_RT] Device stack initialized.");
}

void loop() {
  // Feed the TinyUSB device task
  tud_arduino_task();

  // Check if the host requested a DFU detach via USB control request
  if (dfu_detach_requested) {
    dfu_detach_requested = false;
    ARDUINO_TINYUSB_CONSOLE.println("[DFU_RT] Host requested DFU detach!");
    ARDUINO_TINYUSB_CONSOLE.println("[DFU_RT] Rebooting into DFU bootloader...");
    delay(100);
    tud_remote_wakeup();  // Hint to host; actual reboot is board-specific
    // Board-specific reboot into bootloader
#if defined(ARDUINO_TINYUSB_BOARD_DUE)
    // Due: use the SAM-BA watchdog trick
    *((volatile uint32_t *)0x400E1A34) = 0xA500000D;  // RSTC_CR: use WDT
#elif defined(ARDUINO_TINYUSB_BOARD_ZERO)
    // Zero: call the bootloader entry
    ((void (*)(void))(*(uint32_t *)0x00007C08))();
#elif defined(ARDUINO_TINYUSB_BOARD_GIGA)
    // GIGA: NVIC system reset
    NVIC_SystemReset();
#elif defined(ARDUINO_TINYUSB_BOARD_PICO)
    // Pico: warm-reboot into the USB (UF2) bootloader
    rp2040.rebootToBootloader();
#elif defined(ARDUINO_TINYUSB_BOARD_ESP32)
    // ESP32: software restart (ROM download mode, not USB DFU)
    ESP.restart();
#else
    NVIC_SystemReset();
#endif
  }

  // Allow serial command 'd' to trigger detach as well
  if (ARDUINO_TINYUSB_CONSOLE.available()) {
    char c = ARDUINO_TINYUSB_CONSOLE.read();
    if (c == 'd' || c == 'D') {
      ARDUINO_TINYUSB_CONSOLE.println("[DFU_RT] Serial command: detaching...");
      delay(100);
#if defined(ARDUINO_TINYUSB_BOARD_DUE)
      *((volatile uint32_t *)0x400E1A34) = 0xA500000D;
#elif defined(ARDUINO_TINYUSB_BOARD_ZERO)
      ((void (*)(void))(*(uint32_t *)0x00007C08))();
#elif defined(ARDUINO_TINYUSB_BOARD_GIGA)
      NVIC_SystemReset();
#elif defined(ARDUINO_TINYUSB_BOARD_PICO)
      rp2040.rebootToBootloader();
#elif defined(ARDUINO_TINYUSB_BOARD_ESP32)
      ESP.restart();
#else
      NVIC_SystemReset();
#endif
    }
  }
}
