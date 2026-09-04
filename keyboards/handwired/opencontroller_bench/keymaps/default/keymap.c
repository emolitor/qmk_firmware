// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

// Key 0 is the Nucleo's B1 USER button or virtual bit 0; keys 1-7 are virtual
// bits 1-7 (see bench.h). F24 does nothing on any desktop, so its reports are
// safe to deliver to whatever the dongle is plugged into. Caps Lock is the one
// key whose delivery the host reports back (the 5A LED frame), which makes it
// the end-to-end oracle: one press toggles the LED, a duplicated press does not.
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(KC_F24, KC_CAPS, OC_SLEEP, OC_PAIR, OC_2G4, OC_USB, OC_AUTO, OC_UNPAIR),
};
