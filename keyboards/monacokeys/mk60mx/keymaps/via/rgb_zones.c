// Copyright 2026 MonacoKeys
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include "eeconfig.h"
#include "via.h"
#include <lib/lib8tion/lib8tion.h>

enum underglow_effect {
    UNDERGLOW_SOLID,
    UNDERGLOW_BREATHING,
    UNDERGLOW_RAINBOW_MOVING_CHEVRON,
    UNDERGLOW_CYCLE_ALL,
    UNDERGLOW_CYCLE_LEFT_RIGHT,
    UNDERGLOW_HUE_BREATHING,
    UNDERGLOW_HUE_WAVE,
    UNDERGLOW_GRADIENT_LEFT_RIGHT,
    UNDERGLOW_DUAL_BEACON,
    UNDERGLOW_RIVERFLOW,
    UNDERGLOW_EFFECT_COUNT,
};

enum via_zone_value {
    id_underglow_enable = 1,
    id_backlight_enable,
    id_underglow_effect,
    id_underglow_brightness,
    id_underglow_color,
    id_underglow_speed,
};

typedef struct PACKED {
    uint8_t backlight_enable;
    uint8_t underglow_enable;
    uint8_t underglow_effect;
    uint8_t underglow_hue;
    uint8_t underglow_sat;
    uint8_t underglow_brightness;
    uint8_t underglow_speed;
} rgb_zone_config_t;

STATIC_ASSERT(sizeof(rgb_zone_config_t) == EECONFIG_KB_DATA_SIZE, "RGB zone EECONFIG size is out of spec.");

static rgb_zone_config_t zone_config;
static bool              underglow_fallback_active;
static bool              underglow_clear_pending;
static uint8_t           underglow_render_iter;
static const led_point_t underglow_center = RGB_MATRIX_CENTER;

static void zone_config_save(void) {
    eeconfig_update_kb_datablock(&zone_config, 0, sizeof(zone_config));
}

static void zone_config_set_defaults(void) {
    zone_config = (rgb_zone_config_t){
        .backlight_enable     = 1,
        .underglow_enable     = 1,
        .underglow_effect     = UNDERGLOW_CYCLE_LEFT_RIGHT,
        .underglow_hue        = 0,
        .underglow_sat        = 255,
        .underglow_brightness = 255,
        .underglow_speed      = 127,
    };
    zone_config_save();
}

void eeconfig_init_user(void) {
    zone_config_set_defaults();
}

void keyboard_post_init_user(void) {
    if (!eeconfig_is_kb_datablock_valid()) {
        eeconfig_init_kb_datablock();
        zone_config_set_defaults();
        return;
    }

    eeconfig_read_kb_datablock(&zone_config, 0, sizeof(zone_config));

    bool changed = false;
    if (zone_config.backlight_enable > 1) {
        zone_config.backlight_enable = 1;
        changed                      = true;
    }
    if (zone_config.underglow_enable > 1) {
        zone_config.underglow_enable = 1;
        changed                      = true;
    }
    if (zone_config.underglow_effect >= UNDERGLOW_EFFECT_COUNT) {
        zone_config.underglow_effect = UNDERGLOW_CYCLE_LEFT_RIGHT;
        changed                      = true;
    }
    if (changed) {
        zone_config_save();
    }
}

static hsv_t underglow_hsv_for_led(uint8_t led) {
    hsv_t hsv = {
        .h = zone_config.underglow_hue,
        .s = zone_config.underglow_sat,
        .v = scale8(zone_config.underglow_brightness, RGB_MATRIX_MAXIMUM_BRIGHTNESS),
    };
    uint8_t time = scale16by8(g_rgb_timer, qadd8(zone_config.underglow_speed / 4, 1));

    switch (zone_config.underglow_effect) {
        case UNDERGLOW_SOLID:
            break;
        case UNDERGLOW_BREATHING:
            hsv.v = scale8(abs8(sin8(time / 2) - 128) * 2, hsv.v);
            break;
        case UNDERGLOW_RAINBOW_MOVING_CHEVRON:
            hsv.h += abs8(g_led_config.point[led].y - underglow_center.y) + (g_led_config.point[led].x - time);
            break;
        case UNDERGLOW_CYCLE_ALL:
            hsv.h = time;
            break;
        case UNDERGLOW_CYCLE_LEFT_RIGHT:
            hsv.h = g_led_config.point[led].x - time;
            break;
        case UNDERGLOW_HUE_BREATHING:
            hsv.h += scale8(abs8(sin8(time / 2) - 128) * 2, 12);
            break;
        case UNDERGLOW_HUE_WAVE:
            hsv.h += scale8(abs8(g_led_config.point[led].x - time), 24);
            break;
        case UNDERGLOW_GRADIENT_LEFT_RIGHT: {
            uint8_t scale = scale8(64, zone_config.underglow_speed);
            hsv.h += (scale * g_led_config.point[led].x) >> 5;
            break;
        }
        case UNDERGLOW_DUAL_BEACON: {
            uint16_t beacon_time = scale16by8(g_rgb_timer, zone_config.underglow_speed / 4);
            int8_t   cos_value   = cos8(beacon_time) - 128;
            int8_t   sin_value   = sin8(beacon_time) - 128;
            hsv.h += ((g_led_config.point[led].y - underglow_center.y) * cos_value + (g_led_config.point[led].x - underglow_center.x) * sin_value) / 128;
            break;
        }
        case UNDERGLOW_RIVERFLOW: {
            uint8_t river_time = scale16by8(g_rgb_timer + (led * 315), zone_config.underglow_speed / 8);
            hsv.v              = scale8(abs8(sin8(river_time) - 128) * 2, hsv.v);
            break;
        }
    }

    return hsv;
}

static void render_underglow_range(uint8_t led_min, uint8_t led_max) {
    for (uint8_t i = led_min; i < led_max; i++) {
        if (!HAS_ANY_FLAGS(g_led_config.flags[i], LED_FLAG_UNDERGLOW)) {
            continue;
        }

        rgb_t rgb = hsv_to_rgb(underglow_hsv_for_led(i));
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
}

static void clear_zone_range(uint8_t led_min, uint8_t led_max, uint8_t zone_flag) {
    for (uint8_t i = led_min; i < led_max; i++) {
        if (HAS_ANY_FLAGS(g_led_config.flags[i], zone_flag)) {
            rgb_matrix_set_color(i, 0, 0, 0);
        }
    }
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    if (zone_config.underglow_enable) {
        render_underglow_range(led_min, led_max);
    } else {
        clear_zone_range(led_min, led_max, LED_FLAG_UNDERGLOW);
        if (led_max == RGB_MATRIX_LED_COUNT) {
            underglow_clear_pending = false;
        }
    }

    if (!zone_config.backlight_enable) {
        clear_zone_range(led_min, led_max, LED_FLAG_KEYLIGHT);
    }
    return true;
}

void housekeeping_task_user(void) {
    static uint32_t last_render;

    bool stock_pipeline_active = rgb_matrix_is_enabled() && rgb_matrix_get_mode() != RGB_MATRIX_NONE;
    if (stock_pipeline_active) {
        underglow_fallback_active = false;
        underglow_render_iter     = 0;
        return;
    }

    if (rgb_matrix_get_suspend_state()) {
        underglow_fallback_active = false;
        underglow_render_iter     = 0;
        return;
    }

    if (!zone_config.underglow_enable) {
        if (underglow_fallback_active || underglow_clear_pending) {
            clear_zone_range(0, RGB_MATRIX_LED_COUNT, LED_FLAG_UNDERGLOW);
            rgb_matrix_update_pwm_buffers();
        }
        underglow_fallback_active = false;
        underglow_clear_pending   = false;
        underglow_render_iter     = 0;
        return;
    }

    if (!underglow_fallback_active) {
        underglow_fallback_active = true;
        underglow_render_iter     = 0;
    }

    if (timer_elapsed32(last_render) < RGB_MATRIX_LED_FLUSH_LIMIT) {
        return;
    }
    last_render = timer_read32();

    struct rgb_matrix_limits_t limits = rgb_matrix_get_limits(underglow_render_iter);
    render_underglow_range(limits.led_min_index, limits.led_max_index);
    rgb_matrix_update_pwm_buffers();

    if (limits.led_max_index >= RGB_MATRIX_LED_COUNT) {
        underglow_render_iter = 0;
    } else {
        underglow_render_iter++;
    }
}

static bool zone_config_set_value(uint8_t *data, uint8_t length) {
    if (length < 2) {
        return false;
    }

    switch (data[0]) {
        case id_underglow_enable:
            zone_config.underglow_enable = data[1] != 0;
            underglow_clear_pending      = !zone_config.underglow_enable;
            return true;
        case id_backlight_enable:
            zone_config.backlight_enable = data[1] != 0;
            return true;
        case id_underglow_effect:
            if (data[1] >= UNDERGLOW_EFFECT_COUNT) {
                return false;
            }
            zone_config.underglow_effect = data[1];
            return true;
        case id_underglow_brightness:
            zone_config.underglow_brightness = data[1];
            return true;
        case id_underglow_color:
            if (length < 3) {
                return false;
            }
            zone_config.underglow_hue = data[1];
            zone_config.underglow_sat = data[2];
            return true;
        case id_underglow_speed:
            zone_config.underglow_speed = data[1];
            return true;
        default:
            return false;
    }
}

static bool zone_config_get_value(uint8_t *data, uint8_t length) {
    if (length < 2) {
        return false;
    }

    switch (data[0]) {
        case id_underglow_enable:
            data[1] = zone_config.underglow_enable;
            return true;
        case id_backlight_enable:
            data[1] = zone_config.backlight_enable;
            return true;
        case id_underglow_effect:
            data[1] = zone_config.underglow_effect;
            return true;
        case id_underglow_brightness:
            data[1] = zone_config.underglow_brightness;
            return true;
        case id_underglow_color:
            if (length < 3) {
                return false;
            }
            data[1] = zone_config.underglow_hue;
            data[2] = zone_config.underglow_sat;
            return true;
        case id_underglow_speed:
            data[1] = zone_config.underglow_speed;
            return true;
        default:
            return false;
    }
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    if (length < 2 || data[1] != id_custom_channel) {
        if (length > 0) {
            data[0] = id_unhandled;
        }
        return;
    }

    bool handled = false;
    switch (data[0]) {
        case id_custom_set_value:
            handled = zone_config_set_value(&data[2], length - 2);
            break;
        case id_custom_get_value:
            handled = zone_config_get_value(&data[2], length - 2);
            break;
        case id_custom_save:
            zone_config_save();
            handled = true;
            break;
    }

    if (!handled) {
        data[0] = id_unhandled;
    }
}
