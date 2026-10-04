/*
 * tusb_config_arduinotinyusb.h — THE ACTIVE TinyUSB configuration.
 *
 * This is the only config the stack reads. src/tusb_option.h includes
 * "tusb_config.h", which includes this file, and every translation unit in the
 * build (the vendored TinyUSB sources and your sketch alike) compiles against
 * whatever this file says.
 *
 * Default: the full union — device AND host stacks, every class driver — via
 * the one-line include below. Every bundled example builds that way, so they
 * work out of the box with nothing to copy.
 *
 * SPECIALIZING FOR ONE SKETCH (this is how you cut Flash and RAM)
 * -------------------------------------------------------------
 * Each example ships a ready-made single-role config next to its .ino, e.g.
 * examples/Device_CDC/tusb_config_arduinotinyusb.h. Edit THAT file — it lives
 * in your sketch, so a library update never overwrites your edits — then copy
 * it over this file:
 *
 *   Arduino IDE: copy examples/<Your>/tusb_config_arduinotinyusb.h into
 *     <sketchbook>/libraries/TinyUSB_Arduino/src/tusb_config_arduinotinyusb.h
 *     (overwrite the one-line file that is there now)
 *
 *   arduino-cli / PlatformIO: same copy, e.g.
 *     cp examples/Device_CDC/tusb_config_arduinotinyusb.h \
 *        ~/Arduino/libraries/TinyUSB_Arduino/src/tusb_config_arduinotinyusb.h
 *
 * Mixing and matching classes is just editing the CFG_TUD_* / CFG_TUH_* lines
 * in your copy: set a class to 1 to compile it in. Anything you leave out
 * defaults to 0 (src/tusb_option.h), so it is not compiled at all.
 *
 * TO GO BACK TO THE UNION
 *   cp src/config/tusb_config_union.h src/tusb_config_arduinotinyusb.h
 *
 * This is required before building a different example: a device-role config
 * has CFG_TUH_ENABLED 0, so host examples stop compiling (loudly — see the
 * role guards in src/ArduinoTinyUSB.h), and vice versa.
 *
 * DEBUG LEVEL
 *   CFG_TUSB_DEBUG lives in src/config/tusb_config_common.h (0 = off, the
 *   default; 1 = errors; 2 = verbose). It is #ifndef-guarded, so
 *   -DCFG_TUSB_DEBUG=2 on the compiler command line also works, but note the
 *   Arduino IDE has no UI for extra build flags — edit the header instead.
 *
 * WHY A COPY, AND NOT AN #include?
 * The Arduino build never puts your sketch folder on the include path for
 * library sources, so a config sitting next to your .ino is invisible to the
 * stack. Copying it into src/ is what makes it take effect — and it affects
 * every translation unit, which is required for the setting to be consistent.
 */

#include "config/tusb_config_union.h"
