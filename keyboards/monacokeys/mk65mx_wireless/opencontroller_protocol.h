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

/*
 * Whether the module runs the opt-in UART sleep protocol. Only the 5B 37 92
 * reply to an A6 56 unlock proves it: a stock module ACKs A6 56 like any other
 * frame and then silently ignores every sleep command, so nothing sleep-related
 * is sent until this reads READY.
 */
typedef enum {
    OCP_SLEEP_CAP_UNKNOWN,     /* not negotiated since the last reset or link-up */
    OCP_SLEEP_CAP_PENDING,     /* A6 56 queued, in flight, or awaiting 5B 37 */
    OCP_SLEEP_CAP_UNSUPPORTED, /* A6 56 acknowledged without a 5B 37 */
    OCP_SLEEP_CAP_READY,       /* 5B 37 seen */
} ocp_sleep_capability_t;

/* The host's model of the module's A6 57 auto-sleep flag. */
typedef enum {
    OCP_AUTOSLEEP_OFF,
    OCP_AUTOSLEEP_ARMING, /* A6 57 queued or in flight */
    OCP_AUTOSLEEP_ARMED,
} ocp_autosleep_state_t;

/*
 * The host's model of an explicit A6 54 sleep. ASLEEP ends with the next byte
 * the host sends, which is what wakes the module. A module that is merely
 * auto-sleeping between activity holdoffs is tracked from transmit silence
 * instead and always reads AWAKE here.
 */
typedef enum {
    OCP_MODULE_AWAKE,
    OCP_MODULE_SLEEP_REQUESTED, /* A6 54 queued or in flight */
    OCP_MODULE_ASLEEP,          /* A6 54 acknowledged; RF torn down */
} ocp_module_sleep_state_t;

typedef struct {
    uint16_t rx_checksum_errors;
    uint16_t rx_partial_timeouts;
    uint16_t rx_ack_overflows;
    uint16_t rx_spurious_acks;
    uint16_t tx_ack_timeouts;
    uint16_t control_queue_overflows;
    uint16_t wake_preambles;
    uint16_t keyboard_queue_coalesced; /* full report queue: the newest slot absorbed a state (a transition lost, the final state kept) */
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
 * Queue a 6KRO boot-keyboard report. Reports go out in order while the link
 * is CONNECTED or RECONNECTING (the module's FIFO holds them until the radio
 * link is up). A report identical to the newest queued or sent state is not a
 * transition and is dropped; when the queue is full the newest slot absorbs
 * the new state, so the host still ends where the matrix is.
 */
#define OCP_KEYBOARD_QUEUE_CAPACITY 16
void ocp_set_keyboard_report(const uint8_t report[OCP_KEYBOARD_REPORT_SIZE]);

/**
 * After a reconnect: queue one report of the current matrix state unless a
 * report has been queued or sent since the link went down (those already carry
 * the state). Re-presses a key held across the outage exactly once.
 */
void ocp_begin_keyboard_resync(const uint8_t report[OCP_KEYBOARD_REPORT_SIZE]);

/*
 * Delivery guarantee: every queued transition goes out in order and is
 * retired when the UART accepts it; the in-flight copy is retried up to
 * OCP_TX_MAX_ATTEMPTS and then abandoned by the transaction abort (the
 * module was unresponsive; the link is reset and the resync re-asserts the
 * current state on reconnect). A press whose in-flight copy is abandoned is
 * therefore lost -- the same policy as before, now applying to one queued
 * report instead of the only one.
 */
bool    ocp_keyboard_report_pending(void);
uint8_t ocp_keyboard_queue_count(void);
/** True only when no parser reply, transaction, report or guard is pending. */
bool ocp_is_idle(void);

/** Consume the notification raised when a transaction exhausts its attempts. */
bool ocp_take_tx_failure(void);

/*
 * Sleep protocol. The capability handshake is boot-scoped on the module: it
 * survives transport selection, pairing and unpairing, but a module reset
 * clears it, so every path that may have reset the module (ocp_init(), a
 * transaction abort) drops the capability back to UNKNOWN and the caller must
 * negotiate again before using any sleep command.
 *
 * Waking is transparent to callers: once the capability is READY the service
 * loop precedes the first frame after a period of silence with a discardable
 * 0x00 and a settle gap, because the module wakes on the RX falling edge and
 * loses the byte that carried it.
 */

/** Queue the A6 56 unlock. Re-sending it also turns auto-sleep off again. */
bool ocp_queue_sleep_negotiate(void);

/** Queue the A6 57 arm. Refused unless the capability is READY. */
bool ocp_queue_autosleep_arm(void);

/**
 * Queue the A6 54 explicit sleep behind a release barrier (two queue slots).
 * Refused unless READY and currently awake.
 */
bool ocp_queue_sleep_now(void);

ocp_sleep_capability_t   ocp_get_sleep_capability(void);
ocp_autosleep_state_t    ocp_get_autosleep_state(void);
ocp_module_sleep_state_t ocp_get_module_sleep_state(void);

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
