// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"

#include "bench.h"

/* B1 USER on the NUCLEO-U083RC: PC13, pulled up, low while pressed. */
#define BENCH_BUTTON_PIN C13

void matrix_init_custom(void) {
    gpio_set_pin_input_high(BENCH_BUTTON_PIN);
}

bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    matrix_row_t row = bench_virtual_keys;

    if (!gpio_read_pin(BENCH_BUTTON_PIN)) {
        row |= 1;
    }

    bench_note_matrix((uint8_t)row);

    if (current_matrix[0] == row) {
        return false;
    }
    current_matrix[0] = row;
    return true;
}
