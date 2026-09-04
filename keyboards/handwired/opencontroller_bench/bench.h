// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

#include "opencontroller_protocol.h"

/*
 * Everything a bench script needs lives in a few globals that a debug probe
 * can read and write while the firmware runs. No USB is involved: the
 * NUCLEO-U083RC exposes only its ST-Link, so SWD is the whole bench interface.
 *
 *   bench_virtual_keys   bits 0..7 are ORed into the single matrix row, so a
 *                        probe write presses or releases keys 0..7.
 *   bench_mark           any change is logged as a MARK event, which lets a
 *                        script label the trace with its own step numbers.
 *   bench_status         the driver's observable state, refreshed every main
 *                        loop pass under a sequence counter (seq is odd during
 *                        a rewrite; read it, dump, read it again).
 *   bench_trace          ring of timestamped events: every frame sent to the
 *                        module, every byte received from it, every change
 *                        of the driver's state, every key change, and marks.
 *   bench_trace_head     events written so far; the ring holds the newest
 *                        BENCH_TRACE_CAPACITY of them. An event is complete
 *                        before the head that publishes it is advanced.
 *   bench_raw_tx         [0] is a length, [1..] the bytes: written straight to
 *                        the UART behind the driver's back, then the length is
 *                        cleared. Wiring diagnosis only; the driver's model of
 *                        the module is not told.
 */

#define BENCH_TRACE_CAPACITY 1024
#define BENCH_EVENT_DATA_SIZE 10
#define BENCH_RAW_TX_MAX 15

typedef enum {
    BENCH_EV_NONE  = 0,
    BENCH_EV_TX    = 1, /* data: the frame as written to the UART */
    BENCH_EV_RX    = 2, /* data: consecutive bytes received in one millisecond */
    BENCH_EV_STATE = 3, /* data: see bench_state_snapshot() */
    BENCH_EV_KEYS  = 4, /* data[0]: matrix row after the change */
    BENCH_EV_MARK  = 5, /* data[0..3]: bench_mark, little endian */
    BENCH_EV_RAWTX = 6, /* data: bytes sent from bench_raw_tx */
} bench_event_kind_t;

typedef struct {
    uint32_t t_ms;
    uint8_t  kind;
    uint8_t  len;
    uint8_t  data[BENCH_EVENT_DATA_SIZE];
} bench_event_t;

typedef struct {
    uint32_t          t_ms; /* time of the last change */
    uint8_t           sleep_capability;
    uint8_t           autosleep_state;
    uint8_t           module_sleep_state;
    uint8_t           link_state;
    uint16_t          connection_generation;
    uint8_t           keyboard_leds;
    uint8_t           last_status;
    uint8_t           host; /* connection_get_host() */
    uint8_t           virtual_keys;
    uint8_t           matrix_row;
    uint8_t           reserved;
    ocp_diagnostics_t diagnostics;
    uint16_t          pad;
    uint32_t          seq; /* bumped twice per change (odd while being rewritten); read, dump, read */
} bench_status_t;

extern volatile uint8_t        bench_virtual_keys;
extern volatile uint32_t       bench_mark;
extern volatile bench_status_t bench_status;
extern volatile bench_event_t  bench_trace[BENCH_TRACE_CAPACITY];
extern volatile uint32_t       bench_trace_head;
extern volatile uint8_t        bench_raw_tx[1 + BENCH_RAW_TX_MAX];

/** Called by the custom matrix with the row it just produced. */
void bench_note_matrix(uint8_t row);
