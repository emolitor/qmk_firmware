// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "openboot_bridge.h"

#include <string.h>

#define OBB_CTRL_PAYLOAD_MAX 16
#define OBB_CTRL_QUEUE_CAPACITY 4

/* The shortest ENTER request: op, magic, settle, flags. */
#define OBB_ENTER_REQUEST_LEN 8

static obb_state_t state;
static uint32_t    state_at;
/*
 * The acknowledgement of the OTA command. The CH592 resets immediately after
 * it, so this is the closest observable anchor to the instant the bootloader's
 * idle deadline starts. It is conservative by well under a percent of that
 * deadline, and it is the only anchor the host cannot measure for itself.
 */
static uint32_t ack_at;
static uint32_t activity_at;
static uint32_t tx_stage_at;

static uint16_t settle_ms;
static uint16_t tx_dropped;
static uint16_t rx_dropped;

static uint8_t enter_flags;
static uint8_t last_error;
static uint8_t ocp_link;

static bool ocp_idle;
static bool seized;
static bool enter_response_pending;
static bool ota_request;
static bool seize_request;
static bool release_request;

static uint8_t tx_stage[OBB_MAX_DATA];
static uint8_t tx_stage_len;
static uint8_t rx_stage[OBB_MAX_DATA];
static uint8_t rx_stage_len;

static struct {
    uint8_t payload[OBB_CTRL_PAYLOAD_MAX];
    uint8_t length;
} ctrl_queue[OBB_CTRL_QUEUE_CAPACITY];
static uint8_t ctrl_head;
static uint8_t ctrl_count;

static uint8_t report_scratch[OBB_REPORT_SIZE];

static uint32_t elapsed_since(uint32_t now_ms, uint32_t since_ms) {
    return (uint32_t)(now_ms - since_ms);
}

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value & 0xFF);
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xFF);
    bytes[1] = (uint8_t)((value >> 8) & 0xFF);
    bytes[2] = (uint8_t)((value >> 16) & 0xFF);
    bytes[3] = (uint8_t)((value >> 24) & 0xFF);
}

/*
 * Drop the newest response rather than the oldest when the queue is full: the
 * oldest is the deferred ENTER answer the host is blocked on, while a lost
 * status poll is simply reissued.
 */
static void push_control(const uint8_t *payload, uint8_t length) {
    uint8_t slot;

    if (ctrl_count >= OBB_CTRL_QUEUE_CAPACITY || length > OBB_CTRL_PAYLOAD_MAX) {
        return;
    }

    slot = (uint8_t)((ctrl_head + ctrl_count) % OBB_CTRL_QUEUE_CAPACITY);
    memcpy(ctrl_queue[slot].payload, payload, length);
    ctrl_queue[slot].length = length;
    ctrl_count++;
}

static void push_control_status(uint8_t op, uint8_t status) {
    uint8_t payload[2] = {op, status};

    push_control(payload, sizeof(payload));
}

static void resolve_enter(uint8_t status) {
    if (!enter_response_pending) {
        return;
    }

    enter_response_pending = false;
    push_control_status(OBB_OP_ENTER, status);
}

static void clear_transfers(void) {
    tx_stage_len = 0;
    rx_stage_len = 0;
}

static void release_uart(void) {
    if (!seized) {
        return;
    }

    seized          = false;
    release_request = true;
}

/* Shared by an explicit exit, the watchdog, and USB leaving the configured state. */
static void do_exit(uint32_t now_ms) {
    resolve_enter(OBB_STATUS_E_STATE);
    release_uart();
    clear_transfers();
    state    = OBB_STATE_IDLE;
    state_at = now_ms;
}

static void fail_enter(uint8_t status, uint32_t now_ms) {
    resolve_enter(status);
    release_uart();
    clear_transfers();
    last_error = status;
    state      = OBB_STATE_ERROR;
    state_at   = now_ms;
}

static void begin_settling(uint32_t anchor_ms, uint32_t now_ms) {
    ack_at   = anchor_ms;
    state    = OBB_STATE_SETTLING;
    state_at = now_ms;

    if (!seized) {
        seized        = true;
        seize_request = true;
    }

    resolve_enter(OBB_STATUS_OK);
}

void obb_init(void) {
    state                  = OBB_STATE_IDLE;
    state_at               = 0;
    ack_at                 = 0;
    activity_at            = 0;
    tx_stage_at            = 0;
    settle_ms              = OBB_DEFAULT_SETTLE_MS;
    tx_dropped             = 0;
    rx_dropped             = 0;
    enter_flags            = 0;
    last_error             = OBB_STATUS_OK;
    ocp_link               = 0;
    ocp_idle               = false;
    seized                 = false;
    enter_response_pending = false;
    ota_request            = false;
    seize_request          = false;
    release_request        = false;
    tx_stage_len           = 0;
    rx_stage_len           = 0;
    ctrl_head              = 0;
    ctrl_count             = 0;
}

bool obb_owns_uart(void) {
    return state == OBB_STATE_SETTLING || state == OBB_STATE_PASSTHRU;
}

obb_state_t obb_get_state(void) {
    return state;
}

void obb_set_ocp_state(bool idle, uint8_t link_state) {
    ocp_idle = idle;
    ocp_link = link_state;
}

bool obb_take_ota_request(void) {
    bool requested = ota_request;

    ota_request = false;
    return requested;
}

bool obb_take_seize_request(void) {
    bool requested = seize_request;

    seize_request = false;
    return requested;
}

bool obb_take_release_request(void) {
    bool requested = release_request;

    release_request = false;
    return requested;
}

static void handle_identify(void) {
    uint8_t payload[9];

    payload[0] = OBB_OP_IDENTIFY;
    payload[1] = OBB_STATUS_OK;
    payload[2] = OBB_VERSION_MAJOR;
    payload[3] = OBB_VERSION_MINOR;
    payload[4] = OBB_MAX_DATA;
    write_le16(&payload[5], OBB_DEFAULT_SETTLE_MS);
    payload[7] = (uint8_t)state;
    payload[8] = OBB_CAP_FORCE;

    push_control(payload, sizeof(payload));
}

static void handle_status(uint32_t now_ms) {
    uint8_t  payload[13];
    uint32_t elapsed = 0;

    if (state == OBB_STATE_SETTLING || state == OBB_STATE_PASSTHRU) {
        elapsed = elapsed_since(now_ms, ack_at);
    }

    payload[0] = OBB_OP_STATUS;
    payload[1] = OBB_STATUS_OK;
    payload[2] = (uint8_t)state;
    write_le32(&payload[3], elapsed);
    payload[7] = ocp_link;
    payload[8] = last_error;
    write_le16(&payload[9], tx_dropped);
    write_le16(&payload[11], rx_dropped);

    push_control(payload, sizeof(payload));
}

static void handle_enter(const uint8_t *payload, uint8_t length, uint32_t now_ms) {
    uint16_t requested;

    if (length < OBB_ENTER_REQUEST_LEN) {
        push_control_status(OBB_OP_ENTER, OBB_STATUS_E_ARG);
        return;
    }

    if (read_le32(&payload[1]) != OBB_ENTER_MAGIC) {
        push_control_status(OBB_OP_ENTER, OBB_STATUS_E_MAGIC);
        return;
    }

    if (state != OBB_STATE_IDLE && state != OBB_STATE_ERROR) {
        push_control_status(OBB_OP_ENTER, OBB_STATUS_E_STATE);
        return;
    }

    requested = read_le16(&payload[5]);
    if (requested == 0) {
        requested = OBB_DEFAULT_SETTLE_MS;
    } else if (requested > OBB_MAX_SETTLE_MS) {
        requested = OBB_MAX_SETTLE_MS;
    }

    settle_ms              = requested;
    enter_flags            = payload[7];
    last_error             = OBB_STATUS_OK;
    enter_response_pending = true;
    state                  = OBB_STATE_QUIESCE;
    state_at               = now_ms;
    clear_transfers();
}

static void handle_control(const uint8_t *payload, uint8_t length, uint32_t now_ms) {
    if (length == 0) {
        return;
    }

    switch (payload[0]) {
        case OBB_OP_IDENTIFY:
            handle_identify();
            break;
        case OBB_OP_ENTER:
            handle_enter(payload, length, now_ms);
            break;
        case OBB_OP_STATUS:
            handle_status(now_ms);
            break;
        case OBB_OP_EXIT:
            push_control_status(OBB_OP_EXIT, OBB_STATUS_OK);
            do_exit(now_ms);
            break;
        default:
            push_control_status(payload[0], OBB_STATUS_E_ARG);
            break;
    }
}

bool obb_can_accept_host_report(void) {
    return tx_stage_len == 0;
}

void obb_on_host_report(const uint8_t *report, uint8_t length, uint32_t now_ms) {
    uint8_t payload_length;

    if (report == NULL || length != OBB_REPORT_SIZE) {
        return;
    }

    payload_length = report[1];
    activity_at    = now_ms;

    switch (report[0]) {
        case OBB_TAG_CONTROL:
            if (payload_length > OBB_MAX_DATA) {
                return;
            }
            handle_control(&report[2], payload_length, now_ms);
            break;

        case OBB_TAG_DATA:
            if (payload_length > OBB_MAX_DATA) {
                push_control_status(OBB_OP_DATA_ERROR, OBB_STATUS_E_ARG);
                return;
            }
            /* Bytes only reach the UART once passthrough is actually running. */
            if (state != OBB_STATE_PASSTHRU || payload_length == 0) {
                return;
            }
            memcpy(tx_stage, &report[2], payload_length);
            tx_stage_len = payload_length;
            tx_stage_at  = now_ms;
            break;

        default:
            break;
    }
}

bool obb_can_accept_uart_bytes(void) {
    if (state == OBB_STATE_SETTLING) {
        /* Drain and discard: the bootloader emits noise while it comes up. */
        return true;
    }
    return state == OBB_STATE_PASSTHRU && rx_stage_len == 0;
}

void obb_on_uart_bytes(const uint8_t *bytes, uint8_t length, uint32_t now_ms) {
    if (bytes == NULL || length == 0 || length > OBB_MAX_DATA) {
        return;
    }

    if (state != OBB_STATE_PASSTHRU) {
        return;
    }

    if (rx_stage_len != 0) {
        rx_dropped++;
        return;
    }

    memcpy(rx_stage, bytes, length);
    rx_stage_len = length;
    activity_at  = now_ms;
}

void obb_on_ota_acked(uint32_t now_ms) {
    if (state != OBB_STATE_ENTER_SENT) {
        return;
    }
    begin_settling(now_ms, now_ms);
}

void obb_on_ota_failed(uint32_t now_ms) {
    if (state != OBB_STATE_QUIESCE && state != OBB_STATE_ENTER_SENT) {
        return;
    }
    fail_enter(OBB_STATUS_E_NO_ACK, now_ms);
}

void obb_on_usb_inactive(uint32_t now_ms) {
    if (state == OBB_STATE_IDLE) {
        return;
    }
    do_exit(now_ms);
}

static void service_timers(uint32_t now_ms) {
    switch (state) {
        case OBB_STATE_QUIESCE:
            if (!ocp_idle && elapsed_since(now_ms, state_at) < OBB_QUIESCE_TIMEOUT_MS) {
                break;
            }
            ota_request = true;
            if ((enter_flags & OBB_ENTER_FLAG_FORCE) != 0) {
                /* No acknowledgement can arrive if the module is already in the
                 * bootloader, so anchor on now and let the host's probe decide. */
                begin_settling(now_ms, now_ms);
            } else {
                state    = OBB_STATE_ENTER_SENT;
                state_at = now_ms;
            }
            break;

        case OBB_STATE_ENTER_SENT:
            if (elapsed_since(now_ms, state_at) >= OBB_OTA_TIMEOUT_MS) {
                fail_enter(OBB_STATUS_E_NO_ACK, now_ms);
            }
            break;

        case OBB_STATE_SETTLING:
            if (elapsed_since(now_ms, ack_at) >= settle_ms) {
                state       = OBB_STATE_PASSTHRU;
                state_at    = now_ms;
                activity_at = now_ms;
            }
            break;

        case OBB_STATE_PASSTHRU:
            if (elapsed_since(now_ms, activity_at) >= OBB_WATCHDOG_MS) {
                do_exit(now_ms);
            }
            break;

        default:
            break;
    }
}

static void service_uart(uint32_t now_ms, obb_emit_t to_uart, void *context) {
    if (state != OBB_STATE_PASSTHRU || tx_stage_len == 0 || to_uart == NULL) {
        return;
    }

    /* Never split a report across two writes: a gap mid-frame resets the
     * bootloader's receive parser. */
    if (to_uart(tx_stage, tx_stage_len, context)) {
        tx_stage_len = 0;
        return;
    }

    if (elapsed_since(now_ms, tx_stage_at) >= OBB_TX_STAGE_TIMEOUT_MS) {
        tx_dropped++;
        tx_stage_len = 0;
    }
}

static void service_host(obb_emit_t to_host, void *context) {
    if (to_host == NULL) {
        return;
    }

    memset(report_scratch, 0, sizeof(report_scratch));

    if (ctrl_count != 0) {
        report_scratch[0] = OBB_TAG_CONTROL;
        report_scratch[1] = ctrl_queue[ctrl_head].length;
        memcpy(&report_scratch[2], ctrl_queue[ctrl_head].payload, ctrl_queue[ctrl_head].length);

        if (to_host(report_scratch, OBB_REPORT_SIZE, context)) {
            ctrl_head = (uint8_t)((ctrl_head + 1) % OBB_CTRL_QUEUE_CAPACITY);
            ctrl_count--;
        }
        return;
    }

    if (rx_stage_len != 0) {
        report_scratch[0] = OBB_TAG_DATA;
        report_scratch[1] = rx_stage_len;
        memcpy(&report_scratch[2], rx_stage, rx_stage_len);

        if (to_host(report_scratch, OBB_REPORT_SIZE, context)) {
            rx_stage_len = 0;
        }
    }
}

void obb_service(uint32_t now_ms, obb_emit_t to_uart, obb_emit_t to_host, void *context) {
    service_timers(now_ms);
    service_uart(now_ms, to_uart, context);
    service_host(to_host, context);
}
