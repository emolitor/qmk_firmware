// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tunnel between a host HID interface and the OpenController UART, so a PC can
 * speak OpenBoot's protocol to the CH592 without wiring up its serial pins.
 *
 * Report layout, both directions:
 *
 *   byte 0   tag      OBB_TAG_CONTROL or OBB_TAG_DATA
 *   byte 1   len      payload length, 0..OBB_MAX_DATA
 *   byte 2.. payload  trailing bytes are sent zero and ignored on receive
 *
 * The DATA channel is a byte stream, not a frame channel. This module never
 * parses OpenBoot's protocol, so a single protocol frame may span any number of
 * reports in either direction; the host owns SOF hunting, framing and CRC. That
 * keeps a protocol revision on the wire from ever needing a firmware change,
 * and keeps the largest-frame arithmetic from becoming load bearing here.
 *
 * The tag byte exists so a CONTROL response can interleave into the DATA stream
 * on the same endpoint without in-band escaping.
 */
#define OBB_REPORT_SIZE 64
#define OBB_MAX_DATA (OBB_REPORT_SIZE - 2)

#define OBB_TAG_CONTROL 0x00
#define OBB_TAG_DATA 0x01

#define OBB_OP_IDENTIFY 0x00
#define OBB_OP_ENTER 0x01
#define OBB_OP_STATUS 0x02
#define OBB_OP_EXIT 0x03
/* Reported against a malformed DATA report. Never a request opcode. */
#define OBB_OP_DATA_ERROR 0x7F

/*
 * ENTER carries a magic because accepting it costs the wireless link roughly
 * ten seconds of downtime: a stray report from an unrelated process must not be
 * able to knock the module off air.
 */
#define OBB_ENTER_MAGIC 0x4F424231UL /* "OBB1", little endian on the wire */

/*
 * Enter passthrough without waiting for the module to acknowledge the OTA
 * command. Required whenever a previous attempt already left the CH592 in the
 * bootloader: the OTA command is then only SOF-hunt noise, no acknowledgement
 * can ever arrive, and without this flag the bridge would be a dead end.
 */
#define OBB_ENTER_FLAG_FORCE 0x01

#define OBB_CAP_FORCE 0x01

#define OBB_VERSION_MAJOR 1
#define OBB_VERSION_MINOR 0

/*
 * The only settle value with bench evidence behind it. The bootloader drops or
 * corrupts frames for roughly two seconds after its reset, and auto-boots back
 * into the application ten seconds later, so the host is expected to supply a
 * shorter settle and probe adaptively across that window instead.
 */
#define OBB_DEFAULT_SETTLE_MS 6000
/* Leaves the host at least two seconds of the bootloader's ten second budget. */
#define OBB_MAX_SETTLE_MS 8000

#define OBB_QUIESCE_TIMEOUT_MS 100
#define OBB_OTA_TIMEOUT_MS 1000
#define OBB_TX_STAGE_TIMEOUT_MS 100
/*
 * Bounds a wedged bridge without firing on a healthy flash: a whole-image CRC
 * answers a three second host timeout and is retried three times, so anything
 * much shorter than this would abort legitimate traffic.
 */
#define OBB_WATCHDOG_MS 10000

typedef enum {
    OBB_STATUS_OK       = 0,
    OBB_STATUS_E_MAGIC  = 1,
    OBB_STATUS_E_STATE  = 2,
    OBB_STATUS_E_ARG    = 3,
    OBB_STATUS_E_BUSY   = 4,
    OBB_STATUS_E_NO_ACK = 5,
} obb_status_t;

typedef enum {
    OBB_STATE_IDLE       = 0,
    OBB_STATE_QUIESCE    = 1,
    OBB_STATE_ENTER_SENT = 2,
    OBB_STATE_SETTLING   = 3,
    OBB_STATE_PASSTHRU   = 4,
    OBB_STATE_ERROR      = 5,
} obb_state_t;

/**
 * Callback used to submit one report or one UART burst. Returning false leaves
 * the data staged; the next service call offers exactly the same bytes again.
 */
typedef bool (*obb_emit_t)(const uint8_t *data, uint8_t length, void *context);

/** Reset every buffer, timer and state flag. */
void obb_init(void);

/** True while another host report can be staged for the UART. */
bool obb_can_accept_host_report(void);

/** Feed one report received on the host OUT endpoint. */
void obb_on_host_report(const uint8_t *report, uint8_t length, uint32_t now_ms);

/** True while UART bytes can be consumed; false parks them in the driver queue. */
bool obb_can_accept_uart_bytes(void);

/** Feed UART bytes the bridge owns. Discarded unless passthrough is running. */
void obb_on_uart_bytes(const uint8_t *bytes, uint8_t length, uint32_t now_ms);

/**
 * Publish the OpenController link's observable state. Called once per service
 * call so this module needs no OpenController or QMK header of its own.
 */
void obb_set_ocp_state(bool idle, uint8_t link_state);

/**
 * The module acknowledged the OTA command. This is the anchor for the elapsed
 * time reported to the host: the CH592 resets immediately afterwards, and that
 * reset starts the bootloader's idle deadline. The host cannot observe it.
 */
void obb_on_ota_acked(uint32_t now_ms);

/** The OTA command exhausted its attempts without an acknowledgement. */
void obb_on_ota_failed(uint32_t now_ms);

/** USB left the configured state; passthrough cannot survive it. */
void obb_on_usb_inactive(uint32_t now_ms);

/**
 * Advance timers, move at most one staged UART burst and emit at most one host
 * report. Call once per main loop iteration, in every state.
 */
void obb_service(uint32_t now_ms, obb_emit_t to_uart, obb_emit_t to_host, void *context);

/** True while the bridge, and not the OpenController protocol, owns the UART. */
bool obb_owns_uart(void);

obb_state_t obb_get_state(void);

/** Consume the request to send the OpenController OTA control command. */
bool obb_take_ota_request(void);

/** Consume the request to quiesce the keyboard and seize the UART. */
bool obb_take_seize_request(void);

/** Consume the request to restore the keyboard and hand the UART back. */
bool obb_take_release_request(void);

#ifdef __cplusplus
}
#endif
