// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

// Same as `default`, except key 0 is F13 instead of F24.
//
// F24 is the right default: it does nothing on any desktop. But macOS produces
// no CGEvent for it, so a host-side event tap cannot see it arrive -- which
// makes per-key DELIVERY unmeasurable from the host. F13 (HID usage 0x68 ->
// macOS virtual keycode 105) is surfaced by the event stream while still doing
// nothing on a stock desktop, so `bench.py tap 0` becomes an end-to-end
// delivery oracle that does not depend on the Caps Lock LED round trip.
//
// Use this keymap when measuring whether a key REACHED the host; use `default`
// for everything else.
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(KC_F13, KC_CAPS, OC_SLEEP, OC_PAIR, OC_2G4, OC_USB, OC_AUTO, OC_UNPAIR),
};
