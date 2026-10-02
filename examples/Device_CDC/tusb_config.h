// tusb_config.h — Device_CDC (upstream style: config lives with the example).
// Selected via -DARDUINO_TINYUSB_CONFIG_FILE (absolute path, mapped only by our
// src/tusb_option.h). Arduino IDE builds (no flags) fall back to
// src/tusb_config_arduinotinyusb.h.
// ESP32: the esp32 core + its IDF-bundled TinyUSB both do
// #include "tusb_config.h" expecting THEIR config, and the sketch
// dir precedes it on the -I path — so they land here. Delegate to
// the real core config; our vendored stack never reads this file
// on ESP32 (src/tusb_option.h resolves src/tusb_config.h first).
#if defined(ARDUINO_ARCH_ESP32)
  #include_next "tusb_config.h"
#else
#ifndef ARDUINO_TINYUSB_EXAMPLE_CONFIG_H
#define ARDUINO_TINYUSB_EXAMPLE_CONFIG_H
#define CFG_TUD_ENABLED 1
#define CFG_TUH_ENABLED 0
#define CFG_TUD_CDC     1
#include "config/tusb_config_common.h"
#endif
#endif // !ARDUINO_ARCH_ESP32
