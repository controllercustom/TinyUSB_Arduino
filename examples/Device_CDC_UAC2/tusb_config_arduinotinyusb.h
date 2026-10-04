/*
 * tusb_config_arduinotinyusb.h — Device_CDC_UAC2.
 *
 * INERT UNTIL COPIED. The Arduino build does not put your sketch folder on the
 * include path for library sources, so this file is never read where it sits.
 * Nothing about this example's behaviour depends on it.
 *
 * Every example builds against the full union out of the box (device AND host
 * stacks, all classes). That costs Flash and RAM — see the numbers in
 * src/config/tusb_config_union.h. To build THIS example lean instead:
 *
 *   1. Edit this file (it is in your sketch, so library updates never
 *      overwrite it). Set CFG_TUD_* / CFG_TUH_* to 1 for the classes you
 *      actually want; anything omitted is not compiled at all.
 *   2. Copy it over the library's active config:
 *
 *        Arduino IDE: copy this file to
 *          <sketchbook>/libraries/TinyUSB_Arduino/src/tusb_config_arduinotinyusb.h
 *        arduino-cli: cp examples/Device_CDC_UAC2/tusb_config_arduinotinyusb.h \
 *          ~/Arduino/libraries/TinyUSB_Arduino/src/tusb_config_arduinotinyusb.h
 *
 *   3. Debug level: CFG_TUSB_DEBUG is in src/config/tusb_config_common.h
 *      (0 = off, 1 = errors, 2 = verbose; #ifndef-guarded, so
 *      -DCFG_TUSB_DEBUG=2 works from a command line).
 *
 * TO GO BACK TO THE UNION (required before building a different example — a
 * single-role config makes the other role's examples stop compiling):
 *
 *        cp src/config/tusb_config_union.h src/tusb_config_arduinotinyusb.h
 *
 * Buffer sizes, audio settings, event queue depth and the debug level are not
 * listed below: they come from config/tusb_config_common.h.
 */

#ifndef ARDUINO_TINYUSB_TUSB_CONFIG_H
#define ARDUINO_TINYUSB_TUSB_CONFIG_H
#define CFG_TUD_ENABLED 1
#define CFG_TUH_ENABLED 0
#define CFG_TUD_CDC     1
#define CFG_TUD_AUDIO   1
#include "config/tusb_config_common.h"
#endif
