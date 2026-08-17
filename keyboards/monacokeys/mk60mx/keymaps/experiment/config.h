#pragma once

/* -------------------------------------------------------------------------
 * Independent underglow / backlight zones (custom VIA menu)
 * ---------------------------------------------------------------------- */

// Reserve a small block of EEPROM (managed by QMK's eeconfig datablock API,
// no manual address math needed) to persist the zone on/off toggles and the
// independent underglow effect/color/speed/brightness, see keymap.c.
#define EECONFIG_KB_DATA_SIZE 7

// Pinned so a genuinely fresh/uninitialized EEPROM (a board that has never
// been configured, e.g. straight off the factory line) boots with the stock
// per-key channel on and showing a moving rainbow rather than silently
// falling back to whatever QMK's own default happens to resolve to. This
// only affects first-ever init — it will not override a board that already
// has different settings saved from prior flashing/testing.
#define RGB_MATRIX_DEFAULT_ON true
#define RGB_MATRIX_DEFAULT_MODE RGB_MATRIX_CYCLE_LEFT_RIGHT
