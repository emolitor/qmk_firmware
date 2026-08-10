// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "opencontroller_protocol.h"

#include <string.h>

#define OCP_HEADER_KEYBOARD 0xA1
#define OCP_HEADER_CONTROL 0xA6
#define OCP_HEADER_LED 0x5A
#define OCP_HEADER_STATUS 0x5B
#define OCP_HEADER_BATTERY 0x5C
#define OCP_HEADER_ACK 0x61

#define OCP_ACK_BYTE_1 0x0D
#define OCP_ACK_BYTE_2 0x0A

#define OCP_STATUS_BATTERY_LOW 0x21
#define OCP_STATUS_BATTERY_CRITICAL 0x22
#define OCP_STATUS_BATTERY_NORMAL 0x23
#define OCP_STATUS_PAIRING 0x31
#define OCP_STATUS_CONNECTED 0x32
#define OCP_STATUS_DISCONNECTED 0x33
#define OCP_STATUS_RECONNECTING 0x35
#define OCP_STATUS_REJECTED 0x36

#define OCP_RX_FRAME_SIZE 3
#define OCP_TX_FRAME_MAX_SIZE 10
#define OCP_CONTROL_QUEUE_CAPACITY 8
#define OCP_REPLY_ACK_CAPACITY 8
#define OCP_RX_PARTIAL_TIMEOUT_MS 20
#define OCP_TX_ACK_TIMEOUT_MS 20
#define OCP_TX_MAX_ATTEMPTS 3
// OpenController may resend a changed HID report for six normal RF polls at
// 28/32768 s each. Eight milliseconds leaves margin for all six polls after
// the UART ACK before an RF teardown command is allowed to follow.
#define OCP_RELEASE_DWELL_MS 8

typedef enum {
    OCP_TX_NONE,
    OCP_TX_CONTROL,
    OCP_TX_KEYBOARD,
    OCP_TX_RESYNC,
    OCP_TX_RELEASE,
} ocp_tx_kind_t;

typedef enum {
    OCP_ACTION_CONTROL,
    OCP_ACTION_RELEASE,
} ocp_action_kind_t;

typedef struct {
    ocp_action_kind_t kind;
    uint8_t           subcommand;
} ocp_action_t;

typedef struct {
    bool          active;
    ocp_tx_kind_t kind;
    uint8_t       data[OCP_TX_FRAME_MAX_SIZE];
    uint8_t       length;
    uint8_t       attempts;
    uint32_t      sent_at;
} ocp_inflight_t;

static uint8_t  rx_buffer[OCP_RX_FRAME_SIZE];
static uint8_t  rx_length;
static uint32_t rx_last_byte_at;

static ocp_action_t action_queue[OCP_CONTROL_QUEUE_CAPACITY];
static uint8_t      action_head;
static uint8_t      action_tail;
static uint8_t      action_count;

static uint8_t keyboard_report[OCP_KEYBOARD_REPORT_SIZE];
static bool    keyboard_report_pending;
static uint8_t resync_report[OCP_KEYBOARD_REPORT_SIZE];
static uint8_t resync_phase;
static bool    resync_active;

static uint8_t        reply_acks_pending;
static ocp_inflight_t inflight;
static bool           tx_quiet;
static uint32_t       tx_quiet_started_at;
static bool           tx_failure_pending;
static bool           release_dwell;
static uint32_t       release_dwell_started_at;

static ocp_link_state_t  link_state;
static ocp_power_state_t power_state;
static uint8_t           keyboard_leds;
static uint8_t           battery_percent;
static uint8_t           last_status;
static uint16_t          connection_generation;
static ocp_diagnostics_t diagnostics;

static uint8_t checksum(const uint8_t *data, uint8_t length) {
    uint8_t sum = 0;

    for (uint8_t i = 0; i < length; ++i) {
        sum = (uint8_t)(sum + data[i]);
    }

    return sum;
}

static bool is_rx_header(uint8_t byte) {
    switch (byte) {
        case OCP_HEADER_LED:
        case OCP_HEADER_STATUS:
        case OCP_HEADER_BATTERY:
        case OCP_HEADER_ACK:
            return true;
        default:
            return false;
    }
}

static void reset_rx_parser(void) {
    rx_length = 0;
}

static void expire_partial_frame(uint32_t now_ms) {
    if (rx_length != 0 && (uint32_t)(now_ms - rx_last_byte_at) >= OCP_RX_PARTIAL_TIMEOUT_MS) {
        reset_rx_parser();
        ++diagnostics.rx_partial_timeouts;
    }
}

static void resync_rx_parser(void) {
    for (uint8_t i = 1; i < rx_length; ++i) {
        if (is_rx_header(rx_buffer[i])) {
            rx_length = (uint8_t)(rx_length - i);
            memmove(rx_buffer, &rx_buffer[i], rx_length);
            return;
        }
    }

    reset_rx_parser();
}

static void queue_reply_ack(void) {
    if (reply_acks_pending < OCP_REPLY_ACK_CAPACITY) {
        ++reply_acks_pending;
    } else {
        ++diagnostics.rx_ack_overflows;
    }
}

static void handle_status(uint8_t status) {
    last_status = status;

    switch (status) {
        case OCP_STATUS_BATTERY_LOW:
            power_state = OCP_POWER_LOW;
            break;
        case OCP_STATUS_BATTERY_CRITICAL:
            power_state = OCP_POWER_CRITICAL;
            break;
        case OCP_STATUS_BATTERY_NORMAL:
            power_state = OCP_POWER_NORMAL;
            break;
        case OCP_STATUS_PAIRING:
            link_state = OCP_LINK_PAIRING;
            break;
        case OCP_STATUS_CONNECTED:
            link_state = OCP_LINK_CONNECTED;
            ++connection_generation;
            break;
        case OCP_STATUS_DISCONNECTED:
            link_state = OCP_LINK_DISCONNECTED;
            break;
        case OCP_STATUS_RECONNECTING:
            link_state = OCP_LINK_RECONNECTING;
            break;
        case OCP_STATUS_REJECTED:
            link_state = OCP_LINK_REJECTED;
            break;
    }
}

static void dispatch_rx_frame(uint32_t now_ms) {
    switch (rx_buffer[0]) {
        case OCP_HEADER_ACK:
            if (inflight.active) {
                if (inflight.kind == OCP_TX_RESYNC) {
                    if (resync_phase < 2) {
                        ++resync_phase;
                    } else {
                        resync_active = false;
                    }
                } else if (inflight.kind == OCP_TX_RELEASE) {
                    release_dwell            = true;
                    release_dwell_started_at = now_ms;
                }
                inflight.active = false;
                inflight.kind   = OCP_TX_NONE;
            } else {
                ++diagnostics.rx_spurious_acks;
            }
            break;
        case OCP_HEADER_LED:
            keyboard_leds = rx_buffer[1];
            queue_reply_ack();
            break;
        case OCP_HEADER_STATUS:
            handle_status(rx_buffer[1]);
            queue_reply_ack();
            break;
        case OCP_HEADER_BATTERY:
            battery_percent = rx_buffer[1];
            queue_reply_ack();
            break;
    }
}

static bool rx_frame_is_valid(void) {
    if (rx_buffer[0] == OCP_HEADER_ACK) {
        return rx_buffer[1] == OCP_ACK_BYTE_1 && rx_buffer[2] == OCP_ACK_BYTE_2;
    }

    return checksum(rx_buffer, 2) == rx_buffer[2];
}

static void process_rx_frame(uint32_t now_ms) {
    if (rx_frame_is_valid()) {
        dispatch_rx_frame(now_ms);
        reset_rx_parser();
        return;
    }

    ++diagnostics.rx_checksum_errors;
    resync_rx_parser();
}

static bool send_frame(const uint8_t *data, uint8_t length, ocp_tx_kind_t kind, uint32_t now_ms, ocp_send_callback_t send, void *context) {
    if (send == NULL || !send(data, length, context)) {
        return false;
    }

    memcpy(inflight.data, data, length);
    inflight.length   = length;
    inflight.kind     = kind;
    inflight.attempts = 1;
    inflight.sent_at  = now_ms;
    inflight.active   = true;
    return true;
}

static void service_reply_ack(ocp_send_callback_t send, void *context) {
    static const uint8_t ack[] = {OCP_HEADER_ACK, OCP_ACK_BYTE_1, OCP_ACK_BYTE_2};

    if (send != NULL && send(ack, sizeof(ack), context)) {
        --reply_acks_pending;
    }
}

static void abort_transaction(uint32_t now_ms) {
    inflight.active = false;
    inflight.kind   = OCP_TX_NONE;

    action_head  = 0;
    action_tail  = 0;
    action_count = 0;

    reply_acks_pending = 0;
    reset_rx_parser();
    resync_active = false;
    resync_phase  = 0;
    release_dwell = false;

    link_state  = OCP_LINK_DISCONNECTED;
    last_status = OCP_STATUS_DISCONNECTED;

    tx_quiet            = true;
    tx_quiet_started_at = now_ms;
    tx_failure_pending  = true;
    ++diagnostics.tx_ack_timeouts;
}

static void service_inflight(uint32_t now_ms, ocp_send_callback_t send, void *context) {
    if ((uint32_t)(now_ms - inflight.sent_at) < OCP_TX_ACK_TIMEOUT_MS) {
        return;
    }

    if (inflight.attempts >= OCP_TX_MAX_ATTEMPTS) {
        abort_transaction(now_ms);
        return;
    }

    if (send != NULL && send(inflight.data, inflight.length, context)) {
        ++inflight.attempts;
        inflight.sent_at = now_ms;
    }
}

static bool service_control(uint8_t subcommand, uint32_t now_ms, ocp_send_callback_t send, void *context) {
    uint8_t frame[] = {OCP_HEADER_CONTROL, subcommand, 0};
    frame[2]        = checksum(frame, 2);

    return send_frame(frame, sizeof(frame), OCP_TX_CONTROL, now_ms, send, context);
}

static bool service_keyboard(const uint8_t *report, ocp_tx_kind_t kind, uint32_t now_ms, ocp_send_callback_t send, void *context) {
    uint8_t frame[OCP_TX_FRAME_MAX_SIZE] = {OCP_HEADER_KEYBOARD};
    bool    sent;

    memcpy(&frame[1], report, OCP_KEYBOARD_REPORT_SIZE);
    frame[OCP_TX_FRAME_MAX_SIZE - 1] = checksum(frame, OCP_TX_FRAME_MAX_SIZE - 1);

    sent = send_frame(frame, sizeof(frame), kind, now_ms, send, context);
    if (sent && kind == OCP_TX_KEYBOARD) {
        keyboard_report_pending = false;
    }
    return sent;
}

static void service_resync(uint32_t now_ms, ocp_send_callback_t send, void *context) {
    static const uint8_t empty_report[OCP_KEYBOARD_REPORT_SIZE] = {0};
    const uint8_t       *report                                 = resync_phase == 1 ? empty_report : resync_report;

    service_keyboard(report, OCP_TX_RESYNC, now_ms, send, context);
}

static void service_action(uint32_t now_ms, ocp_send_callback_t send, void *context) {
    static const uint8_t empty_report[OCP_KEYBOARD_REPORT_SIZE] = {0};
    const ocp_action_t  *action                                 = &action_queue[action_tail];
    bool                 sent;

    if (action->kind == OCP_ACTION_RELEASE) {
        sent = service_keyboard(empty_report, OCP_TX_RELEASE, now_ms, send, context);
    } else {
        sent = service_control(action->subcommand, now_ms, send, context);
    }

    if (sent) {
        action_tail = (uint8_t)((action_tail + 1) % OCP_CONTROL_QUEUE_CAPACITY);
        --action_count;
    }
}

void ocp_init(void) {
    memset(rx_buffer, 0, sizeof(rx_buffer));
    rx_length       = 0;
    rx_last_byte_at = 0;

    memset(action_queue, 0, sizeof(action_queue));
    action_head  = 0;
    action_tail  = 0;
    action_count = 0;

    memset(keyboard_report, 0, sizeof(keyboard_report));
    keyboard_report_pending = false;
    memset(resync_report, 0, sizeof(resync_report));
    resync_phase  = 0;
    resync_active = false;

    reply_acks_pending = 0;
    memset(&inflight, 0, sizeof(inflight));
    tx_quiet                 = false;
    tx_quiet_started_at      = 0;
    tx_failure_pending       = false;
    release_dwell            = false;
    release_dwell_started_at = 0;

    link_state            = OCP_LINK_UNKNOWN;
    power_state           = OCP_POWER_UNKNOWN;
    keyboard_leds         = 0;
    battery_percent       = UINT8_MAX;
    last_status           = 0;
    connection_generation = 0;
    memset(&diagnostics, 0, sizeof(diagnostics));
}

void ocp_feed_byte(uint8_t byte, uint32_t now_ms) {
    expire_partial_frame(now_ms);

    if (rx_length == 0 && !is_rx_header(byte)) {
        return;
    }

    rx_buffer[rx_length++] = byte;
    rx_last_byte_at        = now_ms;

    if (rx_length == OCP_RX_FRAME_SIZE) {
        process_rx_frame(now_ms);
    }
}

void ocp_service(uint32_t now_ms, ocp_send_callback_t send, void *context) {
    expire_partial_frame(now_ms);

    if (reply_acks_pending != 0) {
        service_reply_ack(send, context);
        return;
    }

    if (inflight.active) {
        service_inflight(now_ms, send, context);
        return;
    }

    if (tx_quiet) {
        if ((uint32_t)(now_ms - tx_quiet_started_at) < OCP_TX_ACK_TIMEOUT_MS) {
            return;
        }
        tx_quiet = false;
    }

    if (release_dwell) {
        if ((uint32_t)(now_ms - release_dwell_started_at) < OCP_RELEASE_DWELL_MS) {
            return;
        }
        release_dwell = false;
    }

    if (action_count != 0) {
        service_action(now_ms, send, context);
    } else if (link_state == OCP_LINK_CONNECTED && resync_active) {
        service_resync(now_ms, send, context);
    } else if (link_state == OCP_LINK_CONNECTED && keyboard_report_pending) {
        service_keyboard(keyboard_report, OCP_TX_KEYBOARD, now_ms, send, context);
    }
}

bool ocp_queue_control(uint8_t subcommand) {
    return ocp_queue_control_sequence(&subcommand, 1);
}

bool ocp_queue_control_sequence(const uint8_t *subcommands, uint8_t count) {
    if (subcommands == NULL || count == 0 || count > (uint8_t)(OCP_CONTROL_QUEUE_CAPACITY - action_count)) {
        ++diagnostics.control_queue_overflows;
        return false;
    }

    for (uint8_t i = 0; i < count; ++i) {
        action_queue[action_head] = (ocp_action_t){.kind = OCP_ACTION_CONTROL, .subcommand = subcommands[i]};
        action_head               = (uint8_t)((action_head + 1) % OCP_CONTROL_QUEUE_CAPACITY);
        ++action_count;
    }

    return true;
}

bool ocp_queue_release_then_controls(const uint8_t *subcommands, uint8_t count) {
    uint16_t required = (uint16_t)count + 1;

    if (subcommands == NULL || count == 0 || required > (uint16_t)(OCP_CONTROL_QUEUE_CAPACITY - action_count)) {
        ++diagnostics.control_queue_overflows;
        return false;
    }

    action_queue[action_head] = (ocp_action_t){.kind = OCP_ACTION_RELEASE};
    action_head               = (uint8_t)((action_head + 1) % OCP_CONTROL_QUEUE_CAPACITY);
    ++action_count;

    for (uint8_t i = 0; i < count; ++i) {
        action_queue[action_head] = (ocp_action_t){.kind = OCP_ACTION_CONTROL, .subcommand = subcommands[i]};
        action_head               = (uint8_t)((action_head + 1) % OCP_CONTROL_QUEUE_CAPACITY);
        ++action_count;
    }

    return true;
}

uint8_t ocp_queue_available(void) {
    return (uint8_t)(OCP_CONTROL_QUEUE_CAPACITY - action_count);
}

bool ocp_actions_pending(void) {
    bool action_inflight = inflight.active && (inflight.kind == OCP_TX_CONTROL || inflight.kind == OCP_TX_RELEASE);

    return action_count != 0 || action_inflight || release_dwell;
}

void ocp_set_keyboard_report(const uint8_t report[OCP_KEYBOARD_REPORT_SIZE]) {
    if (report == NULL) {
        memset(keyboard_report, 0, sizeof(keyboard_report));
    } else {
        memcpy(keyboard_report, report, sizeof(keyboard_report));
    }
    keyboard_report_pending = true;
}

void ocp_begin_keyboard_resync(const uint8_t report[OCP_KEYBOARD_REPORT_SIZE]) {
    if (report == NULL) {
        memset(resync_report, 0, sizeof(resync_report));
    } else {
        memcpy(resync_report, report, sizeof(resync_report));
    }

    memcpy(keyboard_report, resync_report, sizeof(keyboard_report));
    keyboard_report_pending = false;
    resync_phase            = 0;
    resync_active           = true;
}

bool ocp_is_idle(void) {
    return rx_length == 0 && reply_acks_pending == 0 && !inflight.active && action_count == 0 && !resync_active && !keyboard_report_pending && !tx_quiet && !release_dwell;
}

bool ocp_resync_is_active(void) {
    return resync_active;
}

bool ocp_take_tx_failure(void) {
    bool failed        = tx_failure_pending;
    tx_failure_pending = false;
    return failed;
}

ocp_link_state_t ocp_get_link_state(void) {
    return link_state;
}

ocp_power_state_t ocp_get_power_state(void) {
    return power_state;
}

uint8_t ocp_get_keyboard_leds(void) {
    return keyboard_leds;
}

uint8_t ocp_get_battery_percent(void) {
    return battery_percent;
}

uint8_t ocp_get_last_status(void) {
    return last_status;
}

uint16_t ocp_get_connection_generation(void) {
    return connection_generation;
}

const ocp_diagnostics_t *ocp_get_diagnostics(void) {
    return &diagnostics;
}
