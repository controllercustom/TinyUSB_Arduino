// tusb_config.h — the library's TinyUSB configuration entry point.
//
// This is a one-line shim that forwards to the unified config. An
// ArduinoTinyUSB_EXAMPLE_CONFIG_H may override the unified config for a
// specific example; that override is selected via
// -DARDUINO_TINYUSB_CONFIG_FILE (see tusb_option.h).
//
// Board auto-detection (ArduinoTinyUSB/board_auto.h) runs first so the config
// works with no -D flags in the IDE as well as on the CLI.
#include "ArduinoTinyUSB/board_auto.h"
#include "tusb_config_arduinotinyusb.h"
