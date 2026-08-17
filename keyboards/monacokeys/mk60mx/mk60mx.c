// Copyright 2026 MonacoKeys
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"

#ifdef RGB_MATRIX_ENABLE
uint8_t rgb_matrix_map_row_column_to_led_kb(uint8_t row, uint8_t column, uint8_t *led_i) {
    static const struct {
        uint8_t row;
        uint8_t column;
        uint8_t led;
    } secondary_leds[] = {
        {0, 13, 83}, {1, 13, 46}, {2, 0, 33}, {2, 13, 47}, {3, 0, 30}, {3, 12, 7}, {4, 6, 16},
    };

    for (uint8_t i = 0; i < ARRAY_SIZE(secondary_leds); i++) {
        if (secondary_leds[i].row == row && secondary_leds[i].column == column) {
            led_i[0] = secondary_leds[i].led;
            return 1;
        }
    }

    return 0;
}
#endif
