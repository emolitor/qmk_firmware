// Copyright 2024 (@priyadie)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#ifdef RGB_MATRIX_ENABLE

// clang-format off

const aw_led g_aw_leds[RGB_MATRIX_LED_COUNT] = {
/* Refer to IS31 manual for these locations
 *   driver
 *   |     R location
 *   |     |          G location
 *   |     |          |          B location
 *   |     |          |          | */
    {0, CS1_SW3,   CS2_SW3,   CS3_SW3   },  //  0  Esc
    {0, CS1_SW4,   CS2_SW4,   CS3_SW4   },  //  1  F1
    {0, CS1_SW5,   CS2_SW5,   CS3_SW5   },  //  2  F2
    {0, CS1_SW6,   CS2_SW6,   CS3_SW6   },  //  3  F3
    {0, CS1_SW7,   CS2_SW7,   CS3_SW7   },  //  4  F4
    {0, CS1_SW8,   CS2_SW8,   CS3_SW8   },  //  5  F5
    {0, CS1_SW9,   CS2_SW9,   CS3_SW9   },  //  6  F6
    {0, CS1_SW10,  CS2_SW10,  CS3_SW10  },  //  7  F7
    {0, CS1_SW11,  CS2_SW11,  CS3_SW11  },  //  8  F8
    {0, CS1_SW12,  CS2_SW12,  CS3_SW12  },  //  9  F9
    {1, CS1_SW1,   CS2_SW1,   CS3_SW1   },  //  10 F10
    {1, CS1_SW2,   CS2_SW2,   CS3_SW2   },  //  11 F11
    {1, CS1_SW3,   CS2_SW3,   CS3_SW3   },  //  12 F12
    {1, CS1_SW4,   CS2_SW4,   CS3_SW4   },  //  13 DEL

    {0, CS4_SW3,   CS5_SW3,   CS6_SW3   },  //  14 Esc
    {0, CS4_SW4,   CS5_SW4,   CS6_SW4   },  //  15 1
    {0, CS4_SW5,   CS5_SW5,   CS6_SW5   },  //  16 2
    {0, CS4_SW6,   CS5_SW6,   CS6_SW6   },  //  17 3
    {0, CS4_SW7,   CS5_SW7,   CS6_SW7   },  //  18 4
    {0, CS4_SW8,   CS5_SW8,   CS6_SW8   },  //  19 5
    {0, CS4_SW9,   CS5_SW9,   CS6_SW9   },  //  20 6
    {0, CS4_SW10,  CS5_SW10,  CS6_SW10  },  //  21 7
    {0, CS4_SW11,  CS5_SW11,  CS6_SW11  },  //  22 8
    {0, CS4_SW12,  CS5_SW12,  CS6_SW12  },  //  23 9
    {1, CS4_SW1,   CS5_SW1,   CS6_SW1   },  //  24 0
    {1, CS4_SW2,   CS5_SW2,   CS6_SW2   },  //  25 -
    {1, CS4_SW3,   CS5_SW3,   CS6_SW3   },  //  26 =
    {1, CS4_SW4,   CS5_SW4,   CS6_SW4   },  //  27 Backspace
    {1, CS4_SW5,   CS5_SW5,   CS6_SW5   },  //  28 Home

    {0, CS7_SW3,   CS8_SW3,   CS9_SW3   },  //  29 Tab
    {0, CS7_SW4,   CS8_SW4,   CS9_SW4   },  //  30 Q
    {0, CS7_SW5,   CS8_SW5,   CS9_SW5   },  //  31 W
    {0, CS7_SW6,   CS8_SW6,   CS9_SW6   },  //  32 E
    {0, CS7_SW7,   CS8_SW7,   CS9_SW7   },  //  33 R
    {0, CS7_SW8,   CS8_SW8,   CS9_SW8   },  //  34 T
    {0, CS7_SW9,   CS8_SW9,   CS9_SW9   },  //  35 Y
    {0, CS7_SW10,  CS8_SW10,  CS9_SW10  },  //  36 U
    {0, CS7_SW11,  CS8_SW11,  CS9_SW11  },  //  37 I
    {0, CS7_SW12,  CS8_SW12,  CS9_SW12  },  //  38 O
    {1, CS7_SW1,   CS8_SW1,   CS9_SW1   },  //  39 P
    {1, CS7_SW2,   CS8_SW2,   CS9_SW2   },  //  40 [
    {1, CS7_SW3,   CS8_SW3,   CS9_SW3   },  //  41 ]
    {1, CS7_SW4,   CS8_SW4,   CS9_SW4   },  //  42 \|
    {1, CS7_SW5,   CS8_SW5,   CS9_SW5   },  //  43 Del

    {0, CS10_SW3,  CS11_SW3,  CS12_SW3  },  //  44 Caps Lock
    {0, CS10_SW4,  CS11_SW4,  CS12_SW4  },  //  45 A
    {0, CS10_SW5,  CS11_SW5,  CS12_SW5  },  //  46 S
    {0, CS10_SW6,  CS11_SW6,  CS12_SW6  },  //  47 D
    {0, CS10_SW7,  CS11_SW7,  CS12_SW7  },  //  48 F
    {0, CS10_SW8,  CS11_SW8,  CS12_SW8  },  //  49 G
    {0, CS10_SW9,  CS11_SW9,  CS12_SW9  },  //  50 H
    {0, CS10_SW10, CS11_SW10, CS12_SW10 },  //  51 J
    {0, CS10_SW11, CS11_SW11, CS12_SW11 },  //  52 K
    {0, CS10_SW12, CS11_SW12, CS12_SW12 },  //  53 L
    {1, CS10_SW1,  CS11_SW1,  CS12_SW1  },  //  54 ;
    {1, CS10_SW2,  CS11_SW2,  CS12_SW2  },  //  55 '
    {1, CS10_SW3,  CS11_SW3,  CS12_SW3  },  //  56 Enter
    {1, CS10_SW5,  CS11_SW5,  CS12_SW5  },  //  57 Home

    {0, CS13_SW3,  CS14_SW3,  CS15_SW3  },  //  58 Shift_L
    {0, CS13_SW5,  CS14_SW5,  CS15_SW5  },  //  59 Z
    {0, CS13_SW6,  CS14_SW6,  CS15_SW6  },  //  60 X
    {0, CS13_SW7,  CS14_SW7,  CS15_SW7  },  //  61 C
    {0, CS13_SW8,  CS14_SW8,  CS15_SW8  },  //  62 V
    {0, CS13_SW9,  CS14_SW9,  CS15_SW9  },  //  63 B
    {0, CS13_SW10, CS14_SW10, CS15_SW10 },  //  64 N
    {0, CS13_SW11, CS14_SW11, CS15_SW11 },  //  65 M
    {0, CS13_SW12, CS14_SW12, CS15_SW12 },  //  66 ,
    {1, CS13_SW1,  CS14_SW1,  CS15_SW1  },  //  67 .
    {1, CS13_SW2,  CS14_SW2,  CS15_SW2  },  //  68 /
    {1, CS13_SW3,  CS14_SW3,  CS15_SW3  },  //  69 Shift_R
    {1, CS13_SW4,  CS14_SW4,  CS15_SW4  },  //  70 Up
    {1, CS13_SW5,  CS14_SW5,  CS15_SW5  },  //  71 END

    {0, CS16_SW3,  CS17_SW3,  CS18_SW3  },  //  72 Ctrl_L
    {0, CS16_SW4,  CS17_SW4,  CS18_SW4  },  //  73 Win_L
    {0, CS16_SW5,  CS17_SW5,  CS18_SW5  },  //  74 Alt_L
    {0, CS16_SW9,  CS17_SW9,  CS18_SW9  },  //  75 Space
    {0, CS16_SW12, CS17_SW12, CS18_SW12 },  //  76 Alt_R
    {1, CS16_SW1,  CS17_SW1,  CS18_SW1  },  //  77 Ctrl_R
    {1, CS16_SW2,  CS17_SW2,  CS18_SW2  },  //  78 FN
    {1, CS16_SW3,  CS17_SW3,  CS18_SW3  },  //  79 Left
    {1, CS16_SW4,  CS17_SW4,  CS18_SW4  },  //  80 Down
    {1, CS16_SW5,  CS17_SW5,  CS18_SW5  },  //  81 Right

    {0, CS16_SW10, CS17_SW10, CS18_SW10 },  //  82

    {0, CS1_SW1,   CS2_SW1,   CS3_SW1   },  //  83
    {0, CS4_SW1,   CS5_SW1,   CS6_SW1   },  //  84
    {0, CS7_SW1,   CS8_SW1,   CS9_SW1   },  //  85
    {0, CS10_SW1, CS11_SW1,   CS12_SW1  },  //  86
    {0, CS13_SW1, CS14_SW1,   CS15_SW1  },  //  87
    {0, CS16_SW1, CS17_SW1,   CS18_SW1  },  //  88

    {0, CS1_SW2,   CS2_SW2,   CS3_SW2   },  //  89

    {0, CS7_SW2,   CS8_SW2,   CS9_SW2   },  //  91
    {0, CS10_SW2, CS11_SW2,   CS12_SW2  },  //  92
    {0, CS4_SW2,   CS5_SW2,   CS6_SW2   },  //  90

    {0, CS13_SW2, CS14_SW2,   CS15_SW2  },  //  93
    {0, CS16_SW2, CS17_SW2,   CS18_SW2  },  //  94

    {1, CS16_SW7, CS17_SW7,   CS18_SW7  },  //  106
    {1, CS13_SW7, CS14_SW7,   CS15_SW7  },  //  105
    {1, CS10_SW7, CS11_SW7,   CS12_SW7  },  //  104
    {1, CS7_SW7,   CS8_SW7,   CS9_SW7   },  //  103
    {1, CS4_SW7,   CS5_SW7,   CS6_SW7   },  //  102
    {1, CS1_SW7,   CS2_SW7,   CS3_SW7   },  //  101
    {1, CS16_SW6, CS17_SW6,   CS18_SW6  },  //  100
    {1, CS13_SW6, CS14_SW6,   CS15_SW6  },  //  99
    {1, CS10_SW6, CS11_SW6,   CS12_SW6  },  //  98
    {1, CS7_SW6,   CS8_SW6,   CS9_SW6   },  //  97
    {1, CS4_SW6,   CS5_SW6,   CS6_SW6   },  //  96
    {1, CS1_SW6,   CS2_SW6,   CS3_SW6   },  //  95
};

// clang-format on

#endif  // #ifdef RGB_MATRIX_ENABLE

// Copyright 2024 Su (@isuua)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#ifdef WIRELESS_ENABLE
#    include "wireless.h"
#endif

typedef union {
    uint32_t raw;
    struct {
        uint8_t flag : 1;
        uint8_t devs : 3;
        uint8_t wasd_enable : 1;
    };
} confinfo_t;
confinfo_t confinfo;

uint32_t post_init_timer = 0x00;
bool inqbat_flag         = false;

void eeconfig_confinfo_update(uint32_t raw) {

    eeconfig_update_kb(raw);
}

uint32_t eeconfig_confinfo_read(void) {

    return eeconfig_read_kb();
}

void eeconfig_confinfo_default(void) {

    confinfo.flag = true;
    confinfo.wasd_enable = false;
#ifdef WIRELESS_ENABLE
    confinfo.devs = DEVS_USB;
#endif

    eeconfig_confinfo_update(confinfo.raw);
}

void eeconfig_confinfo_init(void) {

    confinfo.raw = eeconfig_confinfo_read();
    if (!confinfo.raw) {
        eeconfig_confinfo_default();
    }
}

void keyboard_post_init_kb(void) {

#ifdef CONSOLE_ENABLE
    debug_enable = true;
#endif

    eeconfig_confinfo_init();

#ifdef LED_POWER_EN_PIN
    gpio_set_pin_output(LED_POWER_EN_PIN);
    gpio_write_pin_high(LED_POWER_EN_PIN);
#endif

#ifdef USB_POWER_EN_PIN
    gpio_write_pin_low(USB_POWER_EN_PIN);
    gpio_set_pin_output(USB_POWER_EN_PIN);
#endif

#ifdef WIRELESS_ENABLE
    wireless_init();
    wireless_devs_change(!confinfo.devs, confinfo.devs, false);
    post_init_timer = timer_read32();
#endif

    keyboard_post_init_user();
}

bool dip_switch_update_kb(uint8_t index, bool active) {

    if (dip_switch_update_user(index, active) != true) {
        return false;
    }

    switch (index) {
        case 0: { // win
            set_single_persistent_default_layer(0);
        } break;
        case 1: { // mac
            set_single_persistent_default_layer(2);
        } break;
        default: break;
    }

    return true;
}

#ifdef WIRELESS_ENABLE

void usb_power_connect(void) {

#    ifdef USB_POWER_EN_PIN
    gpio_write_pin_low(USB_POWER_EN_PIN);
#    endif
}

void usb_power_disconnect(void) {

#    ifdef USB_POWER_EN_PIN
    gpio_write_pin_high(USB_POWER_EN_PIN);
#    endif
}

void suspend_power_down_kb(void) {

#    ifdef LED_POWER_EN_PIN
    gpio_write_pin_low(LED_POWER_EN_PIN);
#    endif
    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {

#    ifdef LED_POWER_EN_PIN
    gpio_write_pin_high(LED_POWER_EN_PIN);
#    endif

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

    // auto switching devs
    if (post_init_timer && timer_elapsed32(post_init_timer) >= 100) {
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_BT_EN);  // timeout 30min to sleep in bt mode, enable
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_2G4_EN); // timeout 30min to sleep in 2.4g mode, enable
        wireless_devs_change(!confinfo.devs, confinfo.devs, false);
        post_init_timer = 0x00;
    }
}

uint32_t wls_process_long_press(uint32_t trigger_time, void *cb_arg) {
    uint16_t keycode = *((uint16_t *)cb_arg);

    switch (keycode) {
        case KC_BT1: {
            wireless_devs_change(wireless_get_current_devs(), DEVS_BT1, true);
        } break;
        case KC_BT2: {
            wireless_devs_change(wireless_get_current_devs(), DEVS_BT2, true);
        } break;
        case KC_BT3: {
            wireless_devs_change(wireless_get_current_devs(), DEVS_BT3, true);
        } break;
        case KC_BT4: {
            wireless_devs_change(wireless_get_current_devs(), DEVS_BT4, true);
        } break;
        case KC_BT5: {
            wireless_devs_change(wireless_get_current_devs(), DEVS_BT5, true);
        } break;
        case KC_2G4: {
            wireless_devs_change(wireless_get_current_devs(), DEVS_2G4, true);
        } break;
        default:
            break;
    }

    return 0;
}

bool process_record_wls(uint16_t keycode, keyrecord_t *record) {
    static uint16_t keycode_shadow                     = 0x00;
    static deferred_token wls_process_long_press_token = INVALID_DEFERRED_TOKEN;

    keycode_shadow = keycode;

#    ifndef WLS_KEYCODE_PAIR_TIME
#        define WLS_KEYCODE_PAIR_TIME 3000
#    endif

#    define WLS_KEYCODE_EXEC(wls_dev)                                                                                          \
        do {                                                                                                                   \
            if (record->event.pressed) {                                                                                       \
                wireless_devs_change(wireless_get_current_devs(), wls_dev, false);                                             \
                if (wls_process_long_press_token == INVALID_DEFERRED_TOKEN) {                                                  \
                    wls_process_long_press_token = defer_exec(WLS_KEYCODE_PAIR_TIME, wls_process_long_press, &keycode_shadow); \
                }                                                                                                              \
            } else {                                                                                                           \
                cancel_deferred_exec(wls_process_long_press_token);                                                            \
                wls_process_long_press_token = INVALID_DEFERRED_TOKEN;                                                         \
            }                                                                                                                  \
        } while (false)

    switch (keycode) {
        case KC_BT1: {
            WLS_KEYCODE_EXEC(DEVS_BT1);
        } break;
        case KC_BT2: {
            WLS_KEYCODE_EXEC(DEVS_BT2);
        } break;
        case KC_BT3: {
            WLS_KEYCODE_EXEC(DEVS_BT3);
        } break;
        case KC_BT4: {
            WLS_KEYCODE_EXEC(DEVS_BT4);
        } break;
        case KC_BT5: {
            WLS_KEYCODE_EXEC(DEVS_BT5);
        } break;
        case KC_2G4: {
            WLS_KEYCODE_EXEC(DEVS_2G4);
        } break;
        case KC_USB: {
            if (record->event.pressed) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);
            }
        } break;
        case KC_BATQ: {
            inqbat_flag    = record->event.pressed;
        } break;
        default:
            return true;
    }

    return false;
}
#endif

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {

    if (process_record_user(keycode, record) != true) {
        return false;
    }

#ifdef WIRELESS_ENABLE
    if (process_record_wls(keycode, record) != true) {
        return false;
    }
#endif

    switch (keycode) {
        case KC_WASD: {
            if (record->event.pressed) {
                confinfo.wasd_enable = !confinfo.wasd_enable;
                eeconfig_confinfo_update(confinfo.raw);
            }
            return false;
        } break;
        case KC_W: {
            if (confinfo.wasd_enable) {
                if (record->event.pressed) {
                    register_code(KC_UP);
                } else {
                    unregister_code(KC_UP);
                }
                return false;
            }
            return true;
        } break;
        case KC_A: {
            if (confinfo.wasd_enable) {
                if (record->event.pressed) {
                    register_code(KC_LEFT);
                } else {
                    unregister_code(KC_LEFT);
                }
                return false;
            }
            return true;
        } break;
        case KC_S: {
            if (confinfo.wasd_enable) {
                if (record->event.pressed) {
                    register_code(KC_DOWN);
                } else {
                    unregister_code(KC_DOWN);
                }
                return false;
            }
            return true;
        } break;
        case KC_D: {
            if (confinfo.wasd_enable) {
                if (record->event.pressed) {
                    register_code(KC_RGHT);
                } else {
                    unregister_code(KC_RGHT);
                }
                return false;
            }
            return true;
        } break;
        case KC_UP: {
            if (confinfo.wasd_enable) {
                if (record->event.pressed) {
                    register_code(KC_W);
                } else {
                    unregister_code(KC_W);
                }
                return false;
            }
            return true;
        } break;
        case KC_LEFT: {
            if (confinfo.wasd_enable) {
                if (record->event.pressed) {
                    register_code(KC_A);
                } else {
                    unregister_code(KC_A);
                }
                return false;
            }
            return true;
        } break;
        case KC_DOWN: {
            if (confinfo.wasd_enable) {
                if (record->event.pressed) {
                    register_code(KC_S);
                } else {
                    unregister_code(KC_S);
                }
                return false;
            }
            return true;
        } break;
        case KC_RGHT: {
            if (confinfo.wasd_enable) {
                if (record->event.pressed) {
                    register_code(KC_D);
                } else {
                    unregister_code(KC_D);
                }
                return false;
            }
            return true;
        } break;
        default:
            return true;
    }

    return false;
}

#ifdef RGB_MATRIX_ENABLE

#    ifdef WIRELESS_ENABLE
bool wls_rgb_indicator_reset        = false;
uint32_t wls_rgb_indicator_timer    = 0x00;
uint32_t wls_rgb_indicator_interval = 0;
uint32_t wls_rgb_indicator_times    = 0;
uint32_t wls_rgb_indicator_index    = 0;
RGB wls_rgb_indicator_rgb           = {0};

void rgb_matrix_wls_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {

    wls_rgb_indicator_timer = timer_read32();

    wls_rgb_indicator_index    = index;
    wls_rgb_indicator_interval = interval;
    wls_rgb_indicator_times    = times * 2;
    wls_rgb_indicator_rgb      = rgb;
}

void wireless_devs_change_kb(uint8_t old_devs, uint8_t new_devs, bool reset) {

    wls_rgb_indicator_reset = reset;

    if (confinfo.devs != wireless_get_current_devs()) {
        confinfo.devs = wireless_get_current_devs();
        eeconfig_confinfo_update(confinfo.raw);
    }

    switch (new_devs) {
        case DEVS_BT1: {
            if (reset) {
                rgb_matrix_wls_indicator_set(32, (RGB){RGB_BLUE}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(32, (RGB){RGB_BLUE}, 500, 1);
            }
        } break;
        case DEVS_BT2: {
            if (reset) {
                rgb_matrix_wls_indicator_set(33, (RGB){RGB_BLUE}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(33, (RGB){RGB_BLUE}, 500, 1);
            }
        } break;
        case DEVS_BT3: {
            if (reset) {
                rgb_matrix_wls_indicator_set(34, (RGB){RGB_BLUE}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(34, (RGB){RGB_BLUE}, 500, 1);
            }
        } break;
        case DEVS_2G4: {
            if (reset) {
                rgb_matrix_wls_indicator_set(35, (RGB){RGB_GREEN}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(35, (RGB){RGB_GREEN}, 500, 1);
            }
        } break;
        default:
            break;
    }
}

bool rgb_matrix_wls_indicator_cb(void) {

    if (*md_getp_state() != MD_STATE_CONNECTED) {
        wireless_devs_change_kb(wireless_get_current_devs(), wireless_get_current_devs(), wls_rgb_indicator_reset);
        return true;
    }

    // refresh led
    led_wakeup();

    return false;
}

void rgb_matrix_wls_indicator(void) {

    if (wls_rgb_indicator_timer) {

        if (timer_elapsed32(wls_rgb_indicator_timer) >= wls_rgb_indicator_interval) {
            wls_rgb_indicator_timer = timer_read32();

            if (wls_rgb_indicator_times) {
                wls_rgb_indicator_times--;
            }

            if (wls_rgb_indicator_times <= 0) {
                wls_rgb_indicator_timer = 0x00;
                if (rgb_matrix_wls_indicator_cb() != true) {
                    return;
                }
            }
        }

        if (wls_rgb_indicator_times % 2) {
            rgb_matrix_set_color(wls_rgb_indicator_index, wls_rgb_indicator_rgb.r, wls_rgb_indicator_rgb.g, wls_rgb_indicator_rgb.b);
        } else {
            rgb_matrix_set_color(wls_rgb_indicator_index, 0x00, 0x00, 0x00);
        }
    }

    if (inqbat_flag) {
        for (uint8_t i = 0; i < 10; i++) {
            uint8_t mi_index[10] = RGB_MATRIX_BAT_INDEX_MAP;
            if (i < (*md_getp_bat() / 10)) {
                if (*md_getp_bat() >= 50) {
                    rgb_matrix_set_color(mi_index[i], RGB_GREEN);
                } else if (*md_getp_bat() >= 30) {
                    rgb_matrix_set_color(mi_index[i], RGB_GREEN);
                } else {
                    rgb_matrix_set_color(mi_index[i], RGB_RED);
                }
            } else {
                rgb_matrix_set_color(mi_index[i], 0x00, 0x00, 0x00);
            }
        }
    }
}
#    endif

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {

    if (rgb_matrix_indicators_advanced_user(led_min, led_max) != true) {
        return false;
    }

    if (host_keyboard_led_state().caps_lock) {
        rgb_matrix_set_color(44, RGB_MATRIX_MAXIMUM_BRIGHTNESS, RGB_MATRIX_MAXIMUM_BRIGHTNESS, RGB_MATRIX_MAXIMUM_BRIGHTNESS);
    }

    if (keymap_config.no_gui) {
        rgb_matrix_set_color(73, RGB_MATRIX_MAXIMUM_BRIGHTNESS, RGB_MATRIX_MAXIMUM_BRIGHTNESS, RGB_MATRIX_MAXIMUM_BRIGHTNESS);
    }

#    ifdef WIRELESS_ENABLE
    rgb_matrix_wls_indicator();
#    endif

    return true;
}
#endif
