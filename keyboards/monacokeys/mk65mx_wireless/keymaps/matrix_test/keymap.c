// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT_all(
        KC_ESC,  KC_1,    KC_2,    KC_3,   KC_4,   KC_5,   KC_6,   KC_7,   KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC, KC_DEL,  KC_PGUP,
        KC_TAB,  KC_Q,    KC_W,    KC_E,   KC_R,   KC_T,   KC_Y,   KC_U,   KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS, KC_PGDN,
        KC_CAPS, KC_A,    KC_S,    KC_D,   KC_F,   KC_G,   KC_H,   KC_J,   KC_K,    KC_L,    KC_SCLN, KC_QUOT, KC_ENT,  KC_HOME,
        KC_LSFT, KC_NUBS, KC_Z,    KC_X,   KC_C,   KC_V,   KC_B,   KC_N,   KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT, KC_UP,   KC_END,
        KC_LCTL, KC_LGUI, KC_LALT, KC_SPC, KC_RALT, MO(1), KC_LEFT, KC_DOWN, KC_RGHT
    ),
    [1] = LAYOUT_all(
        KC_GRV,  KC_F1,   KC_F2,   KC_F3,  KC_F4,  KC_F5,  KC_F6,  KC_F7,  KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  _______, QK_BOOT, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______
    )
};

void keyboard_post_init_user(void) {
    debug_enable = true;
    debug_matrix = true;
}

#ifdef RGB_MATRIX_ENABLE
#include "i2c_master.h"
#include "rgb_matrix.h"
#include "is31fl3741.h"

static uint32_t i2c_probe_cb(uint32_t trigger_time, void *cb_arg) {
    static bool reinit_done = false;
    uint8_t page = 0xFD;
    i2c_status_t s = i2c_transmit(0x30 << 1, &page, 1, 100);
    dprintf("i2c probe 0x30: %d\n", (int)s);
    if (!reinit_done) {
        reinit_done = true;
        dprintf("re-running is31fl3741_init_drivers\n");
        is31fl3741_init_drivers();
        dprintf("re-init done\n");
    }
    dprintf("rgb: en=%d mode=%d hsv=%d/%d/%d flags=%02X\n",
            rgb_matrix_config.enable, rgb_matrix_config.mode,
            rgb_matrix_config.hsv.h, rgb_matrix_config.hsv.s, rgb_matrix_config.hsv.v,
            rgb_matrix_get_flags());
    return 5000;
}

void keyboard_post_init_user_rgb(void) {}
#endif

void housekeeping_task_user(void) {
    static bool once = false;
    if (!once && timer_read32() > 3000) {
        once = true;
#ifdef RGB_MATRIX_ENABLE
        defer_exec(10, i2c_probe_cb, NULL);
#endif
    }
}
