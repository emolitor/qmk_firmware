// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OCP_KEYBOARD_REPORT_SIZE 8

typedef enum {
    OCP_LINK_UNKNOWN,
    OCP_LINK_PAIRING,
    OCP_LINK_CONNECTED,
    OCP_LINK_DISCONNECTED,
    OCP_LINK_RECONNECTING,
    OCP_LINK_REJECTED,
} ocp_link_state_t;

typedef enum {
    OCP_POWER_UNKNOWN,
    OCP_POWER_NORMAL,
    OCP_POWER_LOW,
    OCP_POWER_CRITICAL,
} ocp_power_state_t;

typedef struct {
    uint16_t rx_checksum_errors;
    uint16_t rx_partial_timeouts;
    uint16_t rx_ack_overflows;
    uint16_t rx_spurious_acks;
    uint16_t tx_ack_timeouts;
    uint16_t control_queue_overflows;
} ocp_diagnostics_t;

/**
 * Callback used by the protocol state machine to submit one complete frame.
 * Returning false leaves the frame queued and does not consume a retry.
 */
typedef bool (*ocp_send_callback_t)(const uint8_t *data, uint8_t length, void *context);

/** Reset all parser, queue and connection state. */
void ocp_init(void);

/** Feed one received UART byte to the parser. */
void ocp_feed_byte(uint8_t byte, uint32_t now_ms);

/**
 * Advance receive timeouts and transmit state. At most one frame is offered to
 * send in each call, so this function never waits for a protocol ACK.
 */
void ocp_service(uint32_t now_ms, ocp_send_callback_t send, void *context);

/** Queue one OpenController 0xA6 control subcommand. */
bool ocp_queue_control(uint8_t subcommand);

/** Atomically queue an ordered sequence of 0xA6 control subcommands. */
bool ocp_queue_control_sequence(const uint8_t *subcommands, uint8_t count);

/**
 * Atomically queue an empty A1 report followed by an ordered sequence of A6
 * controls. After the empty report is ACKed, subsequent queue entries remain
 * behind a nonblocking RF-delivery dwell.
 */
bool ocp_queue_release_then_controls(const uint8_t *subcommands, uint8_t count);

/** Number of entries currently available in the ordered action queue. */
uint8_t ocp_queue_available(void);

/**
 * True until every queued release/control action has received its ACK. This
 * includes the release-delivery dwell and the final action while it is in
 * flight, after it has already left the FIFO.
 */
bool ocp_actions_pending(void);

/**
 * Replace the pending 6KRO boot-keyboard report. Only the newest report is
 * retained while an earlier frame is awaiting its ACK.
 */
void ocp_set_keyboard_report(const uint8_t report[OCP_KEYBOARD_REPORT_SIZE]);

/** Queue an ACK-driven current/empty/current keyboard state resync. */
void ocp_begin_keyboard_resync(const uint8_t report[OCP_KEYBOARD_REPORT_SIZE]);

/** True only when no parser reply, transaction, report or guard is pending. */
bool ocp_is_idle(void);

/** True while the fixed reconnect report sequence is still in progress. */
bool ocp_resync_is_active(void);

/** Consume the notification raised when a transaction exhausts its attempts. */
bool ocp_take_tx_failure(void);

ocp_link_state_t         ocp_get_link_state(void);
ocp_power_state_t        ocp_get_power_state(void);
uint8_t                  ocp_get_keyboard_leds(void);
uint8_t                  ocp_get_battery_percent(void);
uint8_t                  ocp_get_last_status(void);
uint16_t                 ocp_get_connection_generation(void);
const ocp_diagnostics_t *ocp_get_diagnostics(void);

#ifdef __cplusplus
}
#endif
