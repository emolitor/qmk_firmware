// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bench.h"

#include <stddef.h>
#include <string.h>

#include "quantum.h"

#include "connection.h"
#include "opencontroller_protocol.h"
#include "timer.h"
#include "uart.h"

volatile uint8_t        bench_virtual_keys;
volatile uint32_t       bench_mark;
volatile bench_status_t bench_status;
volatile bench_event_t  bench_trace[BENCH_TRACE_CAPACITY];
volatile uint32_t       bench_trace_head;
volatile uint8_t        bench_raw_tx[1 + BENCH_RAW_TX_MAX];
/* Scheduled autonomous tap of key bit 0: bench.py writes bench_tap_delay_ms (and
 * optionally bench_tap_dur_ms) over SWD; the firmware fires the tap that many ms
 * later, on its own. Lets a keystroke be injected while the USB host is asleep,
 * when bench.py (SWD over the host's ST-Link) cannot run. */
volatile uint32_t       bench_tap_delay_ms;   /* write: arm a tap this many ms from now (0 = idle) */
volatile uint16_t       bench_tap_dur_ms;     /* hold time; defaults to 100 ms if left 0 */
static   uint32_t       bench_tap_at;         /* internal: absolute fire time (0 = disarmed) */

static uint32_t last_mark;
static uint8_t  last_matrix_row;
static uint8_t  last_state[BENCH_EVENT_DATA_SIZE];
static bool     state_logged;

/* Every writer runs on the main thread. The probe reads concurrently, so an
 * event is filled in completely before the head that publishes it moves; a
 * reader that samples the head first never decodes a half-written slot. */
static void push_bytes(uint8_t kind, const uint8_t *data, uint8_t length) {
    volatile bench_event_t *event = &bench_trace[bench_trace_head % BENCH_TRACE_CAPACITY];

    if (length > BENCH_EVENT_DATA_SIZE) {
        length = BENCH_EVENT_DATA_SIZE;
    }
    event->t_ms = timer_read32();
    event->kind = kind;
    event->len  = length;
    for (uint8_t i = 0; i < length; ++i) {
        event->data[i] = data[i];
    }
    __sync_synchronize();
    ++bench_trace_head;
}

/* Hooks compiled into the shared driver by OPENCONTROLLER_TRACE. */
void opencontroller_trace_tx(const uint8_t *data, uint8_t length) {
    push_bytes(BENCH_EV_TX, data, length);
}

void opencontroller_trace_rx(uint8_t byte) {
    uint32_t                now_ms = timer_read32();
    volatile bench_event_t *last;

    // Bytes of one frame arrive back to back; keep them in one event.
    if (bench_trace_head != 0) {
        last = &bench_trace[(bench_trace_head - 1) % BENCH_TRACE_CAPACITY];
        if (last->kind == BENCH_EV_RX && last->t_ms == now_ms && last->len < BENCH_EVENT_DATA_SIZE) {
            // The byte lands before the length that makes it visible.
            last->data[last->len] = byte;
            __sync_synchronize();
            ++last->len;
            return;
        }
    }

    push_bytes(BENCH_EV_RX, &byte, 1);
}

void bench_note_matrix(uint8_t row) {
    if (row == last_matrix_row) {
        return;
    }
    last_matrix_row = row;
    push_bytes(BENCH_EV_KEYS, &row, 1);
}

static void bench_state_snapshot(uint8_t out[BENCH_EVENT_DATA_SIZE]) {
    uint16_t generation = ocp_get_connection_generation();

    out[0] = (uint8_t)ocp_get_sleep_capability();
    out[1] = (uint8_t)ocp_get_autosleep_state();
    out[2] = (uint8_t)ocp_get_module_sleep_state();
    out[3] = (uint8_t)ocp_get_link_state();
    out[4] = (uint8_t)(generation & 0xFF);
    out[5] = (uint8_t)(generation >> 8);
    out[6] = ocp_get_keyboard_leds();
    out[7] = ocp_get_last_status();
    out[8] = (uint8_t)connection_get_host();
    out[9] = (uint8_t)ocp_get_diagnostics()->tx_ack_timeouts;
}

/* Published only when something other than the clock changed, so the sequence
 * counter moves on real transitions and a probe read normally sees it still.
 * t_ms is therefore the time of the last change, not of the last loop pass. */
static void refresh_status(void) {
    const ocp_diagnostics_t *diagnostics = ocp_get_diagnostics();
    bench_status_t           next;

    memset(&next, 0, sizeof(next));
    next.t_ms                  = bench_status.t_ms;
    next.sleep_capability      = (uint8_t)ocp_get_sleep_capability();
    next.autosleep_state       = (uint8_t)ocp_get_autosleep_state();
    next.module_sleep_state    = (uint8_t)ocp_get_module_sleep_state();
    next.link_state            = (uint8_t)ocp_get_link_state();
    next.connection_generation = ocp_get_connection_generation();
    next.keyboard_leds         = ocp_get_keyboard_leds();
    next.last_status           = ocp_get_last_status();
    next.host                  = (uint8_t)connection_get_host();
    next.virtual_keys          = bench_virtual_keys;
    next.matrix_row            = last_matrix_row;
    memcpy(&next.diagnostics, diagnostics, sizeof(*diagnostics));

    if (memcmp((const void *)&bench_status, &next, offsetof(bench_status_t, pad)) == 0) {
        return;
    }
    next.t_ms = timer_read32();

    ++bench_status.seq; // odd: rewrite in progress
    __sync_synchronize();
    memcpy((void *)&bench_status, &next, offsetof(bench_status_t, pad));
    __sync_synchronize();
    ++bench_status.seq; // even: consistent
}

static void service_raw_tx(void) {
    uint8_t length = bench_raw_tx[0];
    uint8_t frame[BENCH_RAW_TX_MAX];

    if (length == 0) {
        return;
    }
    if (length > BENCH_RAW_TX_MAX) {
        length = BENCH_RAW_TX_MAX;
    }
    for (uint8_t i = 0; i < length; ++i) {
        frame[i] = bench_raw_tx[1 + i];
    }
    bench_raw_tx[0] = 0;

    uart_transmit(frame, length);
    push_bytes(BENCH_EV_RAWTX, frame, length);
}

void housekeeping_task_kb(void) {
    uint8_t  state[BENCH_EVENT_DATA_SIZE];
    uint32_t mark = bench_mark;

    /* Bench measurement mode: force the 2.4G (dongle) transport at startup.
     * The driver's default host is AUTO, which resolves to the LOCAL USB whenever
     * this Nucleo's own user USB is plugged into the host (desired_target() in
     * opencontroller.c). Keys then go out the Nucleo's own HID (VID 0x1209) and
     * never traverse the module->dongle path under test -- which silently
     * invalidates any host-side delivery measurement. CONNECTION_HOST_BLUETOOTH
     * always resolves to OC_TARGET_2G4 with no USB fallback, so the dongle is the
     * only keyboard the host can see deliver. Retries until it sticks, then stops
     * so a deliberate OC_USB selection still works. */
    static bool bench_forced_2g4;
    if (!bench_forced_2g4) {
        if (connection_get_host_raw() == CONNECTION_HOST_BLUETOOTH) {
            bench_forced_2g4 = true;
        } else {
            connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
        }
    }

    if (bench_tap_delay_ms != 0u) {               /* arm: convert delay to an absolute fire time */
        bench_tap_at = timer_read32() + bench_tap_delay_ms;
        if (bench_tap_at == 0u) { bench_tap_at = 1u; }
        bench_tap_delay_ms = 0u;
    }
    if (bench_tap_at != 0u) {
        uint16_t dur = bench_tap_dur_ms ? bench_tap_dur_ms : 100u;
        int32_t  since = (int32_t)(timer_read32() - bench_tap_at);
        if (since >= 0) {
            if (since >= (int32_t)dur) { bench_virtual_keys &= (uint8_t)~1u; bench_tap_at = 0u; }
            else                       { bench_virtual_keys |= 1u; }
        }
    }

    if (mark != last_mark) {
        last_mark = mark;
        push_bytes(BENCH_EV_MARK, (const uint8_t *)&mark, sizeof(mark));
    }

    service_raw_tx();

    bench_state_snapshot(state);
    if (!state_logged || memcmp(state, last_state, sizeof(state)) != 0) {
        memcpy(last_state, state, sizeof(state));
        state_logged = true;
        push_bytes(BENCH_EV_STATE, state, sizeof(state));
    }

    refresh_status();
}
