// Copyright 2025 emolitor (github.com/emolitor)
// Copyright 2024 Westberry Technology (ChangZhou) Corp., Ltd
// SPDX-License-Identifier: GPL-2.0-or-later*/

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

// Possible LED states.
enum { LED_OFF = 0, LED_ON = 1, LED_BLINK_SLOW = 2, LED_BLINK_FAST = 3 };

// The size of this should match the number of devices in the device index in
// westberry/wireless/module.h. Default to zero to set all LEDs to a default
// state of OFF.
static uint8_t led_blink_state[7] = {0};

// Implement a circular linked list of devices to support FN+TAB device
// selection
struct devs_list {
    int               devs;
    struct devs_list *next;
};

struct devs_list devs[] = {
    {.devs = DEVS_USB, .next = &devs[1]},
    {.devs = DEVS_BT1, .next = &devs[2]},
    {.devs = DEVS_BT2, .next = &devs[3]},
    {.devs = DEVS_BT3, .next = &devs[4]},
    {.devs = DEVS_2G4, .next = &devs[0]}
};

struct devs_list *current_dev = &devs[0]; // Default circular linked list to USB device

// Hack
void md_send_devinfo(const char *name);

// We use per-key tapping term to allow the wireless keys to have a much
// longer tapping term, therefore a longer hold, to match the default
// firmware behaviour.
uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case LT(0, KC_BT1):
        case LT(0, KC_BT2):
        case LT(0, KC_BT3):
        case LT(0, KC_2G4):
            return WIRELESS_TAPPING_TERM;
        default:
            return TAPPING_TERM;
    }
}

uint32_t led_blink_callback(uint32_t trigger_time, void *cb_arg) {
    static const uint8_t pattern[4] = {0x00, 0xff, 0x0f, 0xaa};
    static uint8_t       phase      = 0;
    phase                           = (phase + 1) % 8;

    // Default all connection indicators to off
    led_blink_state[DEVS_USB] = LED_OFF;
    led_blink_state[DEVS_BT1] = LED_OFF;
    led_blink_state[DEVS_BT2] = LED_OFF;
    led_blink_state[DEVS_BT3] = LED_OFF;
    led_blink_state[DEVS_2G4] = LED_OFF;

    // Set active indicator LED mode
    if (*md_getp_state() == MD_STATE_PAIRING) {
        led_blink_state[confinfo.devs] = LED_BLINK_FAST;
    } else if (confinfo.devs != DEVS_USB && *md_getp_state() != MD_STATE_CONNECTED) {
        led_blink_state[confinfo.devs] = LED_BLINK_SLOW;
    } else {
        led_blink_state[confinfo.devs] = LED_ON;
    }

    uint8_t bit = 1 << phase;
    writePin(ESCAPE_PIN, (pattern[led_blink_state[DEVS_USB]] & bit) != 0);
    writePin(DEVS_BT1_PIN, (pattern[led_blink_state[DEVS_BT1]] & bit) != 0);
    writePin(DEVS_BT2_PIN, (pattern[led_blink_state[DEVS_BT2]] & bit) != 0);
    writePin(DEVS_BT3_PIN, (pattern[led_blink_state[DEVS_BT3]] & bit) != 0);
    writePin(DEVS_2G4_PIN, (pattern[led_blink_state[DEVS_2G4]] & bit) != 0);

    return LED_BLINK_FAST_PERIOD_MS / 2;
}

void keyboard_post_init_kb(void) {
    confinfo.raw = eeconfig_read_kb();
    if (!confinfo.raw) {
        confinfo.flag = true;
        confinfo.devs = DEVS_USB;
        eeconfig_update_kb(confinfo.raw);
    }

    gpio_set_pin_output(ESCAPE_PIN);
    gpio_set_pin_output(DEVS_BT1_PIN);
    gpio_set_pin_output(DEVS_BT2_PIN);
    gpio_set_pin_output(DEVS_BT3_PIN);
    gpio_set_pin_output(DEVS_2G4_PIN);

    wireless_init();
    md_send_devinfo(MD_BT_NAME);
    wait_ms(10);
    wireless_devs_change(!confinfo.devs, confinfo.devs, false);
    post_init_timer = timer_read32();

    defer_exec(LED_BLINK_FAST_PERIOD_MS / 2, led_blink_callback, NULL);

    keyboard_post_init_user();
}

void suspend_wakeup_init_kb(void) {
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
        case KC_USB: {
            wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);
            return false;
        }
        case KC_NXT: {
            if (record->event.pressed) {
                current_dev = current_dev->next;
                wireless_devs_change(wireless_get_current_devs(), current_dev->devs, false);
                return false;
            }
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
        default:
            return true;
    }
}

void wireless_devs_change_kb(uint8_t old_devs, uint8_t new_devs, bool reset) {
    if (confinfo.devs != wireless_get_current_devs()) {
        confinfo.devs = wireless_get_current_devs();
        eeconfig_update_kb(confinfo.raw);
    }
}
