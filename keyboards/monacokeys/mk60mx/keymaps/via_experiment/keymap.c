/* SPDX-License-Identifier: GPL-2.0-or-later */

#include QMK_KEYBOARD_H
#include "via.h"
#include "eeconfig.h"
#include <lib/lib8tion/lib8tion.h>

enum layer_names {
    _BASE,
    _FN1
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [_BASE] = LAYOUT(
        QK_GESC, KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    KC_MINS, KC_EQL,  KC_BSPC, KC_DEL,
        KC_TAB,  KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,    KC_LBRC, KC_RBRC, KC_ENT,
        KC_CAPS, KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,          KC_BSLS,
        KC_LSFT, KC_NUBS, KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_N,    KC_M,    KC_COMM, KC_DOT,  KC_SLSH, KC_RSFT, MO(_FN1),
        KC_LCTL, KC_LGUI, KC_LALT,          KC_SPC,                                                        KC_RALT, KC_RGUI, MO(_FN1), KC_RCTL
    ),

    [_FN1] = LAYOUT(
        QK_BOOT, KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,   KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  KC_F11,  KC_F12,  KC_F13,  _______,
        _______, _______, KC_UP,   _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,
        _______, KC_LEFT, KC_DOWN, KC_RGHT, _______, _______, _______, _______, _______, _______, _______, _______,          _______,
        _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______,          _______, _______, _______,
        _______, _______, _______,          _______, _______, _______,                   _______
    )

};

/* -------------------------------------------------------------------------
 * Independent underglow / backlight zones
 *
 * Backlight (per-key) effect/color/speed/brightness still come from the
 * stock RGB Matrix engine (VIA's normal built-in menu, channel 3 — no
 * firmware code needed for that part, see via.json).
 *
 * Underglow gets its own effect/color/speed/brightness. We get there by
 * calling QMK's internal per-effect render functions (e.g. BREATHING,
 * CYCLE_ALL) directly, restricted to LED_FLAG_UNDERGLOW LEDs via
 * effect_params_t.flags, with rgb_matrix_config temporarily swapped to the
 * underglow-specific hue/sat/val/speed for the duration of the call.
 *
 * These functions are QMK internals, not a public API: each is defined
 * exactly once, with external linkage, inside rgb_matrix.c's own inclusion
 * of animations/rgb_matrix_effects.inc under RGB_MATRIX_CUSTOM_EFFECT_IMPLS.
 * Nothing forward-declares them for outside use, so the extern prototypes
 * below are us relying on an implementation detail — a QMK submodule bump
 * that renames/reworks an effect can silently break the build or the link.
 *
 * rgb_matrix_indicators_advanced_user() runs every frame AFTER the normal
 * effect has already been drawn: first it repaints the underglow LEDs with
 * the second effect (if enabled), then it blanks whichever zone(s) are
 * toggled off. That hook only fires while the stock RGB Matrix pipeline is
 * actively rendering, though — see the housekeeping_task_user() fallback
 * renderer further down for what keeps underglow alive when the backlight
 * channel is set to "All Off" or disabled outright.
 *
 * VIA talks to this through a custom channel (id_custom_channel), handled
 * in via_custom_value_command_kb() below. See keymaps/via_experiment/via.json
 * for the matching custom UI definition.
 * ---------------------------------------------------------------------- */

typedef bool (*rgb_effect_fn_t)(effect_params_t *params);

extern bool SOLID_COLOR(effect_params_t *params);
extern bool BREATHING(effect_params_t *params);
extern bool RAINBOW_MOVING_CHEVRON(effect_params_t *params);
extern bool CYCLE_ALL(effect_params_t *params);
extern bool CYCLE_LEFT_RIGHT(effect_params_t *params);
extern bool HUE_BREATHING(effect_params_t *params);
extern bool HUE_WAVE(effect_params_t *params);
extern bool GRADIENT_LEFT_RIGHT(effect_params_t *params);
extern bool DUAL_BEACON(effect_params_t *params);
extern bool RIVERFLOW(effect_params_t *params);

// Order must match the "Underglow Effect" dropdown options in via.json.
static const rgb_effect_fn_t underglow_effects[] = {
    SOLID_COLOR, BREATHING, RAINBOW_MOVING_CHEVRON, CYCLE_ALL, CYCLE_LEFT_RIGHT,
    HUE_BREATHING, HUE_WAVE, GRADIENT_LEFT_RIGHT, DUAL_BEACON, RIVERFLOW,
};
#define UNDERGLOW_EFFECT_COUNT (sizeof(underglow_effects) / sizeof(underglow_effects[0]))

typedef struct __attribute__((packed)) {
    bool    backlight_enable;
    bool    underglow_enable;
    uint8_t underglow_effect;
    uint8_t underglow_hue;
    uint8_t underglow_sat;
    uint8_t underglow_brightness;
    uint8_t underglow_speed;
} rgb_zone_config_t;

static rgb_zone_config_t zone_config;

enum via_zone_value {
    id_underglow_enable = 1,
    id_backlight_enable,
    id_underglow_effect,
    id_underglow_brightness,
    id_underglow_color,
    id_underglow_speed,
};

static void zone_config_eeconfig_save(void) {
    eeconfig_update_kb_datablock(&zone_config, 0, sizeof(zone_config));
}

void keyboard_post_init_user(void) {
    if (!eeconfig_is_kb_datablock_valid()) {
        eeconfig_init_kb_datablock();
        zone_config = (rgb_zone_config_t){
            .backlight_enable     = true,
            .underglow_enable     = true,
            .underglow_effect     = 4, // CYCLE_LEFT_RIGHT — matches RGB_MATRIX_DEFAULT_MODE
                                        // in config.h so both zones boot into a moving
                                        // rainbow, visible enough for factory LED QC.
            .underglow_hue        = 0,
            .underglow_sat        = 255,
            .underglow_brightness = 255,
            .underglow_speed      = 127,
        };
        zone_config_eeconfig_save();
    } else {
        eeconfig_read_kb_datablock(&zone_config, 0, sizeof(zone_config));
    }
}

// Set whenever the underglow effect/zone changes, so the next render call
// tells the effect function this is a fresh start (some effects seed
// internal state, e.g. raindrops, on their first init'd frame).
static bool    underglow_needs_init  = true;
static uint8_t underglow_render_iter = 0;

static void render_underglow_effect(void) {
    uint8_t effect_index = zone_config.underglow_effect;
    if (effect_index >= UNDERGLOW_EFFECT_COUNT) {
        return;
    }

    rgb_config_t saved_config = rgb_matrix_config;
    rgb_matrix_config.hsv.h   = zone_config.underglow_hue;
    rgb_matrix_config.hsv.s   = zone_config.underglow_sat;
    rgb_matrix_config.hsv.v   = scale8(zone_config.underglow_brightness, RGB_MATRIX_MAXIMUM_BRIGHTNESS);
    rgb_matrix_config.speed   = zone_config.underglow_speed;

    // RGB_MATRIX_LED_PROCESS_LIMIT is always ceil(LED_COUNT / 5), so the
    // stock renderer always covers the full LED array over exactly 5 calls.
    // We mirror that cadence here for our own zone so the underglow effect
    // sweeps its whole range every 5 frames, same as the main effect does.
    effect_params_t params = {
        .iter  = underglow_render_iter,
        .flags = LED_FLAG_UNDERGLOW,
        .init  = underglow_needs_init,
    };
    underglow_effects[effect_index](&params);
    underglow_render_iter = (underglow_render_iter + 1) % 5;
    underglow_needs_init  = false;

    rgb_matrix_config = saved_config;
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    if (zone_config.underglow_enable) {
        render_underglow_effect();
    }

    for (uint8_t i = led_min; i < led_max; i++) {
        uint8_t flags = g_led_config.flags[i];

        if ((flags & LED_FLAG_UNDERGLOW) && !zone_config.underglow_enable) {
            rgb_matrix_set_color(i, 0, 0, 0);
        } else if ((flags & LED_FLAG_KEYLIGHT) && !zone_config.backlight_enable) {
            rgb_matrix_set_color(i, 0, 0, 0);
        }
    }
    return true;
}

/* -------------------------------------------------------------------------
 * Underglow fallback renderer
 *
 * rgb_matrix_indicators_advanced_user() above only runs while QMK's stock
 * RGB Matrix pipeline is actively rendering a frame — i.e. rgb_matrix_config
 * .enable is true AND .mode != RGB_MATRIX_NONE. The instant the backlight
 * channel is switched to "All Off" (mode 0) or disabled entirely, QMK's own
 * rgb_matrix_task() blanks every LED once and stops calling any indicator
 * hook, so the underglow zone would go dark too even though it has its own
 * enable flag and is meant to be independent of the backlight channel.
 *
 * housekeeping_task_user() runs every main loop pass regardless of RGB
 * Matrix state, so we use it as a backup driver: whenever the stock
 * pipeline isn't already covering the underglow zone, render and flush it
 * ourselves, on our own throttled cadence. In the normal case (backlight
 * channel on), this does nothing — the overlay above already handles it.
 * ---------------------------------------------------------------------- */
static bool underglow_fallback_active = false;

void housekeeping_task_user(void) {
    static uint32_t last_render = 0;

    bool stock_pipeline_active = rgb_matrix_config.enable && rgb_matrix_config.mode != RGB_MATRIX_NONE;

    if (stock_pipeline_active || !zone_config.underglow_enable || rgb_matrix_get_suspend_state()) {
        underglow_fallback_active = false;
        return;
    }

    if (!underglow_fallback_active) {
        // Just took over from the stock pipeline (or from being fully idle) —
        // treat this as a fresh start so effects that seed state on their
        // first init'd frame don't pick up stale iteration state.
        underglow_needs_init      = true;
        underglow_fallback_active = true;
    }

    if (timer_elapsed32(last_render) < RGB_MATRIX_LED_FLUSH_LIMIT) {
        return;
    }
    last_render = timer_read32();

    render_underglow_effect();
    rgb_matrix_update_pwm_buffers();
}

static void zone_config_set_value(uint8_t *data) {
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch (*value_id) {
        case id_underglow_enable:
            zone_config.underglow_enable = value_data[0] != 0;
            break;
        case id_backlight_enable:
            zone_config.backlight_enable = value_data[0] != 0;
            break;
        case id_underglow_effect:
            zone_config.underglow_effect = value_data[0];
            underglow_needs_init         = true;
            break;
        case id_underglow_brightness:
            zone_config.underglow_brightness = value_data[0];
            break;
        case id_underglow_color:
            zone_config.underglow_hue = value_data[0];
            zone_config.underglow_sat = value_data[1];
            break;
        case id_underglow_speed:
            zone_config.underglow_speed = value_data[0];
            break;
    }
}

static void zone_config_get_value(uint8_t *data) {
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch (*value_id) {
        case id_underglow_enable:
            value_data[0] = zone_config.underglow_enable;
            break;
        case id_backlight_enable:
            value_data[0] = zone_config.backlight_enable;
            break;
        case id_underglow_effect:
            value_data[0] = zone_config.underglow_effect;
            break;
        case id_underglow_brightness:
            value_data[0] = zone_config.underglow_brightness;
            break;
        case id_underglow_color:
            value_data[0] = zone_config.underglow_hue;
            value_data[1] = zone_config.underglow_sat;
            break;
        case id_underglow_speed:
            value_data[0] = zone_config.underglow_speed;
            break;
    }
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    uint8_t *command_id        = &(data[0]);
    uint8_t *channel_id        = &(data[1]);
    uint8_t *value_id_and_data = &(data[2]);

    if (*channel_id == id_custom_channel) {
        switch (*command_id) {
            case id_custom_set_value:
                zone_config_set_value(value_id_and_data);
                break;
            case id_custom_get_value:
                zone_config_get_value(value_id_and_data);
                break;
            case id_custom_save:
                zone_config_eeconfig_save();
                break;
            default:
                *command_id = id_unhandled;
                break;
        }
        return;
    }

    *command_id = id_unhandled;
}
