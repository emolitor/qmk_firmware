// Copyright 2025 emolitor (github.com/emolitor)
// Copyright 2024 Westberry Technology (ChangZhou) Corp., Ltd
// Copyright 2024 Wind (@yelishang)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "westberry/wb_bluetooth.h"

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
        case LT(0, BT_PRF1):
            return WIRELESS_TAPPING_TERM;
        case LT(0, BT_PRF2):
            return WIRELESS_TAPPING_TERM;
        case LT(0, BT_PRF3):
            return WIRELESS_TAPPING_TERM;
        case LT(0, OU_USB):
            return WIRELESS_TAPPING_TERM;
        default:
            return TAPPING_TERM;
    }
}

void eeconfig_init_kb(void) {
    confinfo.flag                          = true;
    confinfo.devs                          = DEVS_USB;
    eeconfig_update_kb(confinfo.raw);
    eeconfig_init_user();
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
    if (confinfo.devs == DEVS_USB && gpio_read_pin(BT_CABLE_PIN)) {
        gpio_write_pin_low(USB_POWER_EN_PIN);
    } else {
        gpio_write_pin_high(USB_POWER_EN_PIN);
    }
    gpio_set_pin_output(USB_POWER_EN_PIN);

    md_send_devctrl(MD_SND_CMD_DEVCTRL_FW_VERSION);   // get the module fw version.
    md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_BT_EN);  // timeout 30min to sleep in bt mode, enable
    md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_2G4_EN); // timeout 30min to sleep in 2.4g mode, enable
    wireless_devs_change(!confinfo.devs, confinfo.devs, false);

    keyboard_post_init_user();
}

void suspend_power_down_kb(void) {
    gpio_write_pin_high(LED_POWER_EN_PIN);
    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    gpio_write_pin_low(LED_POWER_EN_PIN);
    rgb_matrix_reload_from_eeprom();

    wireless_devs_change(wireless_get_current_devs(), wireless_get_current_devs(), false);
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
        case RGB_TOG: {
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
                wireless_devs_change(!confinfo.devs, confinfo.devs, false);
            }
            return false;
        }
        case OU_USB: {
            wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);
            return false;
        }
        case LT(0, BT_PRF1): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT1, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT1, true);
            }
            return false;
        }
        case LT(0, BT_PRF2): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT2, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT2, true);
            }
            return false;
        }
        case LT(0, BT_PRF3): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT3, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT3, true);
            }
            return false;
        }
        case LT(0, OU_2P4G): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_2G4, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_2G4, true);
            }
            return false;
        }
    }

    return true;
}

void wireless_devs_change_kb(uint8_t old_devs, uint8_t new_devs, bool reset) {
    if (confinfo.devs != wireless_get_current_devs()) {
        confinfo.devs = wireless_get_current_devs();
        eeconfig_update_kb(confinfo.raw);
    }
}

void blink(uint8_t key_index, uint8_t r, uint8_t g, uint8_t b, bool blink) {
    if (blink) {
        rgb_matrix_set_color(key_index, r, g, b);
    } else {
        rgb_matrix_set_color(key_index, RGB_OFF);
    }
}

void connection_indicators(void) {
    switch (confinfo.devs) {
        case DEVS_BT1: {
            if (*md_getp_state() == MD_STATE_PAIRING) {
                blink(DEVS_BT1_INDEX, RGB_ADJ_WHITE, blink_fast);
            } else if (*md_getp_state() != MD_STATE_CONNECTED) {
                blink(DEVS_BT1_INDEX, RGB_ADJ_WHITE, blink_slow);
            } else {
                rgb_matrix_set_color(DEVS_BT1_INDEX, RGB_ADJ_WHITE);
            }
        } break;
        case DEVS_BT2: {
            if (*md_getp_state() == MD_STATE_PAIRING) {
                blink(DEVS_BT2_INDEX, RGB_ADJ_WHITE, blink_fast);
            } else if (*md_getp_state() != MD_STATE_CONNECTED) {
                blink(DEVS_BT2_INDEX, RGB_ADJ_WHITE, blink_slow);
            } else {
                rgb_matrix_set_color(DEVS_BT2_INDEX, RGB_ADJ_WHITE);
            }
        } break;
        case DEVS_BT3: {
            if (*md_getp_state() == MD_STATE_PAIRING) {
                blink(DEVS_BT3_INDEX, RGB_ADJ_WHITE, blink_fast);
            } else if (*md_getp_state() != MD_STATE_CONNECTED) {
                blink(DEVS_BT3_INDEX, RGB_ADJ_WHITE, blink_slow);
            } else {
                rgb_matrix_set_color(DEVS_BT3_INDEX, RGB_ADJ_WHITE);
            }
        } break;
        case DEVS_2G4: {
            if (*md_getp_state() == MD_STATE_PAIRING) {
                blink(DEVS_2G4_INDEX, RGB_ADJ_WHITE, blink_fast);
            } else if (*md_getp_state() != MD_STATE_CONNECTED) {
                blink(DEVS_2G4_INDEX, RGB_ADJ_WHITE, blink_slow);
            } else {
                rgb_matrix_set_color(DEVS_2G4_INDEX, RGB_ADJ_WHITE);
            }
        } break;
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

        // Show active connection
        connection_indicators();
    } else if (confinfo.devs != DEVS_USB && *md_getp_state() != MD_STATE_CONNECTED) {
        // Always show wireless connection indicators when not connected
        connection_indicators();
    }

    if (host_keyboard_led_state().caps_lock) {
        rgb_matrix_set_color(CAPSLOCK_INDEX, RGB_ADJ_WHITE);
    }

    return true;
}
