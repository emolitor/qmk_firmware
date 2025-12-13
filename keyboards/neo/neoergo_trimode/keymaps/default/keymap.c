// Copyright 2023 JoyLee (@itarze)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

enum layers {
    WIN_B,
    WIN_FN,
    MAC_B,
    MAC_FN,
};

// clang-format off
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [WIN_B] = LAYOUT(
        KC_DEL,  KC_ESC,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_NUHS, KC_BSPC,
        KC_PGUP, KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS,
        KC_PGDN, KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,          KC_ENT,
     MO(WIN_FN), KC_LSFT, KC_NUBS, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_UP,   KC_RSFT,
                 KC_LCTL, KC_LGUI,                            KC_LALT, KC_SPC,  KC_SPC,  KC_RCTL,                            KC_LEFT, KC_DOWN, KC_RGHT
    ),

    [WIN_FN] = LAYOUT(
        EE_CLR, KC_GRV,  KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  _______, _______,
        _______, KC_NXT, LT(0, KC_BT1), LT(0, KC_BT2), LT(0, KC_BT3), LT(0, KC_2G4), _______, _______, KC_USB, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,          _______,
        _______, _______, _______, RGB_TOG, RGB_MOD, RGB_RMOD,RGB_HUI, RGB_HUD, RGB_HUD, RGB_SPI, RGB_SPD, RGB_VAI, RGB_VAD, _______, _______, _______,
                 _______,TO(MAC_B),                           _______, _______, _______, _______,                            _______, _______, _______
    ),

    [MAC_B] = LAYOUT(
        KC_DEL,  KC_ESC,  KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_NUHS, KC_BSPC,
        KC_PGUP, KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_BSLS,
        KC_PGDN, KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,          KC_ENT,
     MO(MAC_FN), KC_LSFT, KC_NUBS, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_UP,   KC_RSFT,
                 KC_LCTL, KC_LALT,                            KC_LGUI, KC_SPC,  KC_SPC,  KC_RCTL,                            KC_LEFT, KC_DOWN, KC_RGHT
    ),

    [MAC_FN] = LAYOUT(
        EE_CLR, KC_GRV,  KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  _______, _______,
        _______, KC_NXT, LT(0, KC_BT1), LT(0, KC_BT2), LT(0, KC_BT3), LT(0, KC_2G4), _______, _______, KC_USB, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,          _______,
        _______, _______, _______, RGB_TOG, RGB_MOD, RGB_RMOD,RGB_HUI, RGB_HUD, RGB_HUD, RGB_SPI, RGB_SPD, RGB_VAI, RGB_VAD, _______, _______, _______,
                 _______, TO(WIN_B),                          _______, _______, _______, _______,                            _______, _______, _______
    )
};

// clang-format on
