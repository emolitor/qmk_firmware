// Copyright 2025 emolitor (github.com/emolitor)
// Copyright 2024 Westberry Technology (ChangZhou) Corp., Ltd
// Copyright 2024 Wind (@yelishang)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bridge75.h"

#ifdef WIRELESS_ENABLE
#include "wireless.h"

typedef union {
    uint32_t raw;
    struct {
        uint8_t devs : 3;
    };
} confinfo_t;
confinfo_t confinfo;

uint32_t post_init_timer = 0x00;

uint8_t bat_level    = 0;
uint8_t blink_index  = 0;
bool    blink_fast   = true;
bool    blink_slow   = true;

// Expose md_send_devinfo to support the Bridge75 Bluetooth naming quirk
// See the readme.md for more information about the quirk.
void md_send_devinfo(const char *name);

// Expose wireless_task and smsg_is_busy to allow for more aggressive
// wireless_task processing and to prevent sleep when smsg_is_busy.
void wireless_task(void);
bool smsg_is_busy(void);

void eeconfig_init_kb(void) {
    confinfo.devs = DEVS_USB;
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

    wireless_init();
    md_send_devinfo(MD_BT_NAME);
    wait_ms(10);
    wireless_devs_change(!confinfo.devs, confinfo.devs, false);
    post_init_timer = timer_read32();

    keyboard_post_init_user();
}

void usb_power_connect(void) {
    gpio_write_pin_low(USB_POWER_EN_PIN);
    wait_ms(5);
}

void usb_power_disconnect(void) {
    gpio_write_pin_high(USB_POWER_EN_PIN);
}

void suspend_power_down_kb(void) {
    gpio_write_pin_high(LED_POWER_EN_PIN);
    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
    gpio_write_pin_low(LED_POWER_EN_PIN);

    wireless_devs_change(wireless_get_current_devs(), wireless_get_current_devs(), false);
    suspend_wakeup_init_user();
    wait_ms(5);
}

bool lpwr_is_allow_timeout_hook(void) {
    if (smsg_is_busy() || gpio_read_pin(BT_CABLE_PIN)) {
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
        case EE_CLR: {
            // Only reset the eeprom on keypress to avoid repeating eeprom
            // clear if held down.
            if (record->event.pressed) {
                eeconfig_init();
                wireless_devs_change(!confinfo.devs, confinfo.devs, false);
            }
            return false;
        }
        case BT_USB: {
            wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);
            return false;
        }
        case LT(0, BT_HOST1): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT1, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT1, true);
            }
            return false;
        }
        case LT(0, BT_HOST2): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT2, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT2, true);
            }
            return false;
        }
        case LT(0, BT_HOST3): {
            if (record->tap.count && record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT3, false);
            } else if (record->event.pressed && *md_getp_state() != MD_STATE_PAIRING) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT3, true);
            }
            return false;
        }
        case LT(0, BT_2_4G): {
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
    uint8_t current_layer = get_highest_layer(default_layer_state | layer_state);
    blink_index = blink_index + 1;
    blink_fast  = (blink_index % 64 == 0) ? !blink_fast : blink_fast;
    blink_slow  = (blink_index % 128 == 0) ? !blink_slow : blink_slow;

    if (!rgb_matrix_indicators_advanced_user(led_min, led_max)) {
        return false;
    }

    // When in Layer 1 show the UX
    if ((current_layer == 1) || (current_layer == 3)) {
        // Set all mapped keys to orange
        uint8_t layer = get_highest_layer(layer_state);
        for (uint8_t row = 0; row < MATRIX_ROWS; ++row) {
            for (uint8_t col = 0; col < MATRIX_COLS; ++col) {
                uint8_t index = g_led_config.matrix_co[row][col];

                if (index >= led_min && index < led_max && index != NO_LED && keymap_key_to_keycode(layer, (keypos_t){col, row}) > KC_TRNS) {
                    if (current_layer == 3) {
                        rgb_matrix_set_color(index, RGB_ADJ_BLUE);
                    } else {
                        rgb_matrix_set_color(index, RGB_ADJ_ORANGE);
                    }
                }
            }
        }

        if (gpio_read_pin(BT_CABLE_PIN) && !gpio_read_pin(BT_CHARGE_PIN)) {
            // Check if we are plugged in and charging
            rgb_matrix_set_color(ESCAPE_INDEX, RGB_ADJ_WHITE);
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

    return true;
}

// Exprimental change to fix duplicate and hung key presses on wireless
void wireless_send_nkro(report_nkro_t *report) {
    static report_keyboard_t temp_report_keyboard                 = {0};
    uint8_t                  wls_report_nkro[MD_SND_CMD_NKRO_LEN] = {0};

    if (MD_STATE_PAIRING == *md_getp_state()) {
        return;
    }

#ifdef NKRO_ENABLE
    if (report != NULL) {
        report_nkro_t temp_report_nkro = *report;
        uint8_t       key_count        = 0;

        temp_report_keyboard.mods = temp_report_nkro.mods;
        for (uint8_t i = 0; i < NKRO_REPORT_BITS; i++) {
            key_count += __builtin_popcount(temp_report_nkro.bits[i]);
        }

        //
        // Use NKRO for sending when more than 6 keys are pressed
        // to solve the issue of the lack of a protocol flag in wireless mode.
        //

        for (uint8_t i = 0; i < key_count; i++) {
            uint8_t usageid;
            uint8_t idx, n = 0;

            for (n = 0; n < NKRO_REPORT_BITS && !temp_report_nkro.bits[n]; n++) {
            }
            usageid = (n << 3) | biton(temp_report_nkro.bits[n]);
            del_key_bit(&temp_report_nkro, usageid);

            for (idx = 0; idx < WLS_KEYBOARD_REPORT_KEYS; idx++) {
                if (temp_report_keyboard.keys[idx] == usageid) {
                    goto next;
                }
            }

            for (idx = 0; idx < WLS_KEYBOARD_REPORT_KEYS; idx++) {
                if (temp_report_keyboard.keys[idx] == 0x00) {
                    temp_report_keyboard.keys[idx] = usageid;
                    break;
                }
            }
        next:
            if (idx == WLS_KEYBOARD_REPORT_KEYS && (usageid < (MD_SND_CMD_NKRO_LEN * 8))) {
                wls_report_nkro[usageid / 8] |= 0x01 << (usageid % 8);
            }
        }

        temp_report_nkro = *report;

        // find key up and del it.
        uint8_t nkro_keys = key_count;
        for (uint8_t i = 0; i < WLS_KEYBOARD_REPORT_KEYS; i++) {
            report_nkro_t found_report_nkro;
            uint8_t       usageid = 0x00;
            uint8_t       n;

            found_report_nkro = temp_report_nkro;

            for (uint8_t c = 0; c < nkro_keys; c++) {
                for (n = 0; n < NKRO_REPORT_BITS && !found_report_nkro.bits[n]; n++) {
                }
                usageid = (n << 3) | biton(found_report_nkro.bits[n]);
                del_key_bit(&found_report_nkro, usageid);
                if (usageid == temp_report_keyboard.keys[i]) {
                    del_key_bit(&temp_report_nkro, usageid);
                    nkro_keys--;
                    break;
                }
            }

            if (usageid != temp_report_keyboard.keys[i]) {
                temp_report_keyboard.keys[i] = 0x00;
            }
        }

    } else {
        memset(&temp_report_keyboard, 0, sizeof(temp_report_keyboard));
    }
#endif

    if (smsg_is_busy()) {
        wireless_task();
    }

    extern host_driver_t wireless_driver;
    wireless_driver.send_keyboard(&temp_report_keyboard);
    md_send_nkro(wls_report_nkro);
}

// Temporarily moved to lpws_wb32.c
// Deep Sleep hack, reboot on wake
//void lpwr_clock_enable_user(void) {
//    mcu_reset();
//}

#endif

bool rgb_matrix_indicators_kb(void) {
    if (!rgb_matrix_indicators_user()) {
        return false;
    }
    if (host_keyboard_led_state().caps_lock ) {
        rgb_matrix_set_color(CAPSLOCK_INDEX, 0xFF, 0xFF, 0xFF);
    }
    return true;
}
