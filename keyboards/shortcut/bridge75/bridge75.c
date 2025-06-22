// Copyright 2025 emolitor (github.com/emolitor)
// Copyright 2024 Westberry Technology (ChangZhou) Corp., Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "wireless.h"

typedef union {
    uint32_t raw;
    struct {
        uint8_t flag : 1;
        uint8_t devs : 3;
    };
} confinfo_t;
confinfo_t confinfo;

uint32_t post_init_timer = 0x00;

#ifdef RGB_MATRIX_ENABLE
uint8_t  blink_index     = 0;
bool     blink_fast      = true;
bool     blink_slow      = true;
bool     rgb_override    = false;
#endif

// Expose md_send_devinfo to support the Bridge75 Bluetooth naming quirk
// See the readme.md for more information about the quirk.
void md_send_devinfo(const char *name);

// We use per-key tapping term to allow the wireless keys to have a much
// longer tapping term, therefore a longer hold, to match the default
// firmware behaviour.
uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case LT(0, KC_BT1):
            return WIRELESS_TAPPING_TERM;
        case LT(0, KC_BT2):
            return WIRELESS_TAPPING_TERM;
        case LT(0, KC_BT3):
            return WIRELESS_TAPPING_TERM;
        case LT(0, KC_2G4):
            return WIRELESS_TAPPING_TERM;
        case LT(0, KC_NO):
            return TAPPING_TERM;
        default:
            return TAPPING_TERM;
    }
}

void eeconfig_init_kb(void) {
    confinfo.flag = true;
    confinfo.devs = DEVS_USB;
    eeconfig_update_kb(confinfo.raw);
    eeconfig_init_user();
}

void keyboard_post_init_kb(void) {
    confinfo.raw = eeconfig_read_kb();
    if (!confinfo.raw) {
        eeconfig_init_kb();
    }

    #ifdef RGB_MATRIX_ENABLE
    gpio_set_pin_output(LED_POWER_EN_PIN);
    gpio_write_pin_low(LED_POWER_EN_PIN);
    #endif

    gpio_write_pin_low(USB_POWER_EN_PIN);
    gpio_set_pin_output(USB_POWER_EN_PIN);

    // Set GPIO as high input for battery charging state
    gpio_set_pin_input(BT_CABLE_PIN);
    gpio_set_pin_input_high(BT_CHARGE_PIN);

    wireless_init();
    md_send_devinfo(MD_BT_NAME);
    wait_ms(10);
    wireless_devs_change(!confinfo.devs, confinfo.devs, false);
    post_init_timer = timer_read32();

    keyboard_post_init_user();
}

void usb_power_connect(void) {
    gpio_write_pin_low(USB_POWER_EN_PIN);
}

void usb_power_disconnect(void) {
    gpio_write_pin_high(USB_POWER_EN_PIN);
}

void suspend_power_down_kb(void) {
    #ifdef RGB_MATRIX_ENABLE
    gpio_write_pin_high(LED_POWER_EN_PIN);
    #endif

    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    #ifdef RGB_MATRIX_ENABLE
    gpio_write_pin_low(LED_POWER_EN_PIN);
    #endif

    wireless_devs_change(wireless_get_current_devs(), wireless_get_current_devs(), false);
    suspend_wakeup_init_user();
}

bool lpwr_is_allow_timeout_hook(void) {
    if (wireless_get_current_devs() == DEVS_USB) {
        return false;
    }

    return true;
}

void wireless_post_task(void) {
    if (post_init_timer && timer_elapsed32(post_init_timer) >= 100) {
        md_send_devctrl(MD_SND_CMD_DEVCTRL_FW_VERSION);   // get the module fw version.
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_BT_EN);  // timeout 30min to sleep in bt mode, enable
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_2G4_EN); // timeout 30min to sleep in 2.4g mode, enable
        wireless_devs_change(!confinfo.devs, confinfo.devs, false);
        post_init_timer = 0x00;
    }
}

void md_devs_change(uint8_t devs, bool reset) {
    switch (devs) {
        case DEVS_USB: {
            md_send_devctrl(MD_SND_CMD_DEVCTRL_USB);
        } break;
        case DEVS_2G4: {
            md_send_devctrl(MD_SND_CMD_DEVCTRL_2G4);
            if (reset) {
                md_send_devctrl(MD_SND_CMD_DEVCTRL_PAIR);
            }
        } break;
        case DEVS_BT1: {
            md_send_devctrl(MD_SND_CMD_DEVCTRL_BT1);
            if (reset) {
                md_send_devctrl(MD_SND_CMD_DEVCTRL_PAIR);
            }
        } break;
        case DEVS_BT2: {
            md_send_devctrl(MD_SND_CMD_DEVCTRL_BT2);
            if (reset) {
                md_send_devctrl(MD_SND_CMD_DEVCTRL_PAIR);
            }
        } break;
        case DEVS_BT3: {
            md_send_devctrl(MD_SND_CMD_DEVCTRL_BT3);
            if (reset) {
                md_send_devctrl(MD_SND_CMD_DEVCTRL_PAIR);
            }
        } break;
        default:
            break;
    }
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (process_record_user(keycode, record) != true) {
        return false;
    }

    switch (keycode) {
        #ifdef RGB_MATRIX_ENABLE
        case MO(1): {
            // Enable RGB temporarily when FN is pressed to show indicators
            if (record->event.pressed && !rgb_matrix_is_enabled()) {
                rgb_override = true;
                rgb_matrix_toggle_noeeprom();
                rgb_matrix_sethsv_noeeprom(HSV_OFF);
            } else if (rgb_override) {
                rgb_override = false;
                rgb_matrix_toggle_noeeprom();
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
        #endif
        case EE_CLR: {
            // Only reset the eeprom on keypress to avoid repeating eeprom
            // clear if held down.
            if (record->event.pressed) {
                eeconfig_init();
                wireless_devs_change(!confinfo.devs, confinfo.devs, false);
            }
            return false;
        }
        case KC_USB: {
            wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);
            return false;
        }
        case LT(0, KC_BT1): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT1, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT1, true);
            }
            return false;
        }
        case LT(0, KC_BT2): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT2, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT2, true);
            }
            return false;
        }
        case LT(0, KC_BT3): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT3, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT3, true);
            }
            return false;
        }
        case LT(0, KC_2G4): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_2G4, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_2G4, true);
            }
            return false;
        }
        #ifdef VIA_ENABLE
        case LT(0, KC_NO): {
            // Rather than using layers the default firmware uses dynamic key
            // remapping to switch between WIN (Default) and MAC modes
            if (!record->tap.count && record->event.pressed) {
                if (dynamic_keymap_get_keycode(0, 5, 1) == KC_LALT) {
                    // Switch to WIN mode (Default)
                    dynamic_keymap_set_keycode(0, 5, 1, KC_LGUI);
                    dynamic_keymap_set_keycode(0, 5, 2, KC_LALT);
                    dynamic_keymap_set_keycode(0, 5, 9, KC_RALT);
                } else if (dynamic_keymap_get_keycode(0, 5, 1) == KC_LGUI) {
                    // Switch to MAC mode
                    dynamic_keymap_set_keycode(0, 5, 1, KC_LALT);
                    dynamic_keymap_set_keycode(0, 5, 2, KC_LGUI);
                    dynamic_keymap_set_keycode(0, 5, 9, KC_RGUI);
                }
            } else if (record->event.pressed) {
            }
            return false;
        }
        #endif
    }

    return true;
}

void wireless_devs_change_kb(uint8_t old_devs, uint8_t new_devs, bool reset) {
    if (confinfo.devs != wireless_get_current_devs()) {
        confinfo.devs = wireless_get_current_devs();
        eeconfig_update_kb(confinfo.raw);
    }
}

#ifdef RGB_MATRIX_ENABLE
void blink(uint8_t key_index, uint8_t r, uint8_t g, uint8_t b, bool blink) {
    if (blink) {
        rgb_matrix_set_color(key_index, r, g, b);
    } else {
        rgb_matrix_set_color(key_index, RGB_OFF);
    }
}

void connection_indicators(void) {
    switch (confinfo.devs) {
        case DEVS_USB: {
            rgb_matrix_set_color(DEVS_USB_INDEX, RGB_ADJ_WHITE);
        } break;
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

        // Check if we are plugged in
        if (gpio_read_pin(BT_CABLE_PIN)) {
            // We are plugged in
            if (!gpio_read_pin(BT_CHARGE_PIN)) {
                // We are charging blink red
                blink(ESCAPE_INDEX, RGB_ADJ_RED, blink_slow);
            } else {
                // We are fully charged solid green
                rgb_matrix_set_color(ESCAPE_INDEX, RGB_ADJ_GREEN);
            }
        } else {
            uint8_t bat_level = *md_getp_bat();
            if (bat_level > 90) {
                rgb_matrix_set_color(ESCAPE_INDEX, RGB_ADJ_GREEN);
            } else if (bat_level > 50) {
                rgb_matrix_set_color(ESCAPE_INDEX, RGB_ADJ_BLUE);
            } else if (bat_level > 10) {
                rgb_matrix_set_color(ESCAPE_INDEX, RGB_ADJ_YELLOW);
            } else {
                rgb_matrix_set_color(ESCAPE_INDEX, RGB_ADJ_RED);
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

// Temporary work around for WS2812 pin init
void board_init(void) {
    gpio_set_pin_output(WS2812_DI_PIN);
    gpio_write_pin_low(WS2812_DI_PIN);
}
#endif

// Experimental change for force MCU reset on unhandled_exception
void _unhandled_exception(void) {
    mcu_reset();
}
