// Copyright 2025 emolitor (github.com/emolitor)
// Copyright 2024 Westberry Technology (ChangZhou) Corp., Ltd
// Copyright 2024 Wind (@yelishang)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "connection.h"

typedef union {
    uint32_t raw;
    struct {
        uint8_t flag : 1;
        uint8_t devs : 3;
    };
} confinfo_t;
confinfo_t confinfo;

uint8_t bat_level    = 0;
uint8_t blink_index  = 0;
bool    blink_fast   = true;
bool    blink_slow   = true;
bool    rgb_override = false;

// We use per-key tapping term to allow the wireless keys to have a much
// longer tapping term, therefore a longer hold, to match the default
// firmware behaviour.
uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case LT(0, KC_1):
            return WIRELESS_TAPPING_TERM;
        case LT(0, KC_2):
            return WIRELESS_TAPPING_TERM;
        case LT(0, KC_3):
            return WIRELESS_TAPPING_TERM;
        case LT(0, KC_4):
            return WIRELESS_TAPPING_TERM;
        default:
            return TAPPING_TERM;
    }
}

void eeconfig_init_kb(void) {
    confinfo.flag                          = true;
    confinfo.devs                          = 0;
    eeconfig_update_kb(confinfo.raw);
    eeconfig_init_user();
}

uint8_t get_devs(void) {
    return confinfo.devs;
}

void update_devs(uint8_t devs) {
    confinfo.devs = devs;
    eeconfig_update_kb(confinfo.raw);
}

void keyboard_post_init_kb(void) {
    confinfo.raw = eeconfig_read_kb();
    if (!confinfo.raw) {
        eeconfig_init_kb();
    }

    gpio_write_pin_low(LED_POWER_EN_PIN);
    gpio_set_pin_output(LED_POWER_EN_PIN);

    // Set GPIO as high input for battery charging state
    gpio_set_pin_input(BT_CABLE_PIN);
    gpio_set_pin_input_high(BT_CHARGE_PIN);

    // Set USB_POWER_EN_PIN state before enabling the output to avoid instability
    if (gpio_read_pin(BT_CABLE_PIN)) {
        gpio_write_pin_low(USB_POWER_EN_PIN);
    } else {
        gpio_write_pin_high(USB_POWER_EN_PIN);
    }
    gpio_set_pin_output(USB_POWER_EN_PIN);

    keyboard_post_init_user();
}

void suspend_power_down_kb(void) {
    gpio_write_pin_high(LED_POWER_EN_PIN);
    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    gpio_write_pin_low(LED_POWER_EN_PIN);
    rgb_matrix_reload_from_eeprom();

    suspend_wakeup_init_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (process_record_user(keycode, record) != true) {
        return false;
    }

    switch (keycode) {
        case MO(1): {
            // Enable RGB temporarily when FN is pressed to show indicators
            if (record->event.pressed && !rgb_matrix_is_enabled()) {
                rgb_override = true;
                rgb_matrix_enable_noeeprom();
                rgb_matrix_sethsv_noeeprom(HSV_OFF);
            } else if (rgb_override) {
                rgb_override = false;
                rgb_matrix_reload_from_eeprom();
            }
            return true;
        }
        case RM_TOGG: {
            // Restore indicators if in overriden state
            if (rgb_override) {
                rgb_override = false;
                rgb_matrix_reload_from_eeprom();
            }
            return true;
        }
        case EE_CLR: {
            // Only reset the eeprom on keypress to avoid repeating eeprom
            // clear if held down.
            if (record->event.pressed) {
                eeconfig_init();
                connection_set_host(CONNECTION_HOST_USB);
            }
            return false;
        }
    }

    return true;
}

static void blink(uint8_t key_index, uint8_t r, uint8_t g, uint8_t b, bool blink) {
    if (blink) {
        rgb_matrix_set_color(key_index, r, g, b);
    } else {
        rgb_matrix_set_color(key_index, RGB_OFF);
    }
}

void battery_percent_changed_kb(uint8_t level) {
    bat_level = level;
}

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    blink_index = blink_index + 1;
    blink_fast  = (blink_index % 64 == 0) ? !blink_fast : blink_fast;
    blink_slow  = (blink_index % 128 == 0) ? !blink_slow : blink_slow;

    if (!rgb_matrix_indicators_advanced_user(led_min, led_max)) {
        return false;
    }

    // When in Layer 1 show the UX
    if (get_highest_layer(default_layer_state | layer_state) == 1) {
        // Set all mapped keys to orange
        uint8_t layer = get_highest_layer(layer_state);
        for (uint8_t row = 0; row < MATRIX_ROWS; ++row) {
            for (uint8_t col = 0; col < MATRIX_COLS; ++col) {
                uint8_t index = g_led_config.matrix_co[row][col];

                if (index >= led_min && index < led_max && index != NO_LED && keymap_key_to_keycode(layer, (keypos_t){col, row}) > KC_TRNS) {
                    rgb_matrix_set_color(index, RGB_ADJ_ORANGE);
                }
            }
        }

        if (gpio_read_pin(BT_CABLE_PIN) && !gpio_read_pin(BT_CHARGE_PIN)) {
            // Check if we are plugged in and charging
            blink(ESCAPE_INDEX, RGB_ADJ_RED, blink_slow);
        } else {
            if (bat_level > 90) {
                rgb_matrix_set_color(ESCAPE_INDEX, RGB_ADJ_GREEN);
            } else if (bat_level > 50) {
                rgb_matrix_set_color(ESCAPE_INDEX, RGB_ADJ_BLUE);
            } else if (bat_level > 10) {
                rgb_matrix_set_color(ESCAPE_INDEX, RGB_ADJ_YELLOW);
            } else if (bat_level > 0) {
                rgb_matrix_set_color(ESCAPE_INDEX, RGB_ADJ_RED);
            } else {
                // Only show battery if its actually been set
                rgb_matrix_set_color(ESCAPE_INDEX, RGB_OFF);
            }
        }
    }

    if (host_keyboard_led_state().caps_lock) {
        rgb_matrix_set_color(CAPSLOCK_INDEX, RGB_ADJ_WHITE);
    }

    return true;
}
