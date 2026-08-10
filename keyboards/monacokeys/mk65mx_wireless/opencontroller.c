// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H

#include <hal.h>
#include <string.h>

#include "action_util.h"
#include "bluetooth.h"
#include "connection.h"
#include "gpio.h"
#include "opencontroller_protocol.h"
#include "timer.h"
#include "uart.h"
#include "usb_util.h"

#define OPENCONTROLLER_BAUD 115200
#define OPENCONTROLLER_RX_BUDGET 32
#define OPENCONTROLLER_RESELECT_DELAY_MS 1000

#define OC_COMMAND_SELECT_USB 0x11
#define OC_COMMAND_SELECT_2G4 0x30
#define OC_COMMAND_PAIR 0x51
#define OC_COMMAND_UNPAIR 0x52

typedef enum {
    OC_TARGET_UNKNOWN,
    OC_TARGET_USB,
    OC_TARGET_2G4,
} oc_target_t;

typedef enum {
    OC_OPERATION_NONE,
    OC_OPERATION_PAIR,
    OC_OPERATION_UNPAIR,
} oc_operation_t;

static oc_target_t    selected_target;
static bool           reselect_deferred;
static uint32_t       reselect_deferred_at;
static oc_target_t    reselect_target;
static uint16_t       connection_generation;
static oc_operation_t pending_operation;
static bool           operation_enqueued;
static bool           operation_cancelled;
static bool           operation_retry_deferred;
static uint32_t       operation_retry_deferred_at;

/* QMK's generic UART API has no nonblocking transmit operation. Queue an
 * entire protocol frame atomically only when ChibiOS has enough free space;
 * the pure protocol state machine will offer it again on the next task call
 * otherwise. */
static bool opencontroller_uart_try_send(const uint8_t *data, uint8_t length, void *context) {
    size_t written = 0;
    (void)context;

    chSysLock();
    if (oqGetEmptyI(&UART_DRIVER.oqueue) >= length) {
        written = sdWriteI(&UART_DRIVER, data, length);
    }
    chSysUnlock();

    return written == length;
}

static oc_target_t desired_target(void) {
    switch (connection_get_host_raw()) {
        case CONNECTION_HOST_AUTO:
            return usb_connected_state() ? OC_TARGET_USB : OC_TARGET_2G4;
        case CONNECTION_HOST_BLUETOOTH:
            return OC_TARGET_2G4;
        case CONNECTION_HOST_2P4GHZ:
            // This is normalized to BLUETOOTH before routing is serviced.
            return OC_TARGET_2G4;
        case CONNECTION_HOST_NONE:
        case CONNECTION_HOST_USB:
        default:
            return OC_TARGET_USB;
    }
}

static bool select_target(oc_target_t target, bool force) {
    uint8_t command;
    bool    queued;

    if (!force && target == selected_target) {
        return true;
    }

    command = target == OC_TARGET_2G4 ? OC_COMMAND_SELECT_2G4 : OC_COMMAND_SELECT_USB;
    queued  = target == OC_TARGET_USB ? ocp_queue_release_then_controls(&command, 1) : ocp_queue_control(command);
    if (!queued) {
        return false;
    }

    selected_target   = target;
    reselect_deferred = false;
    return true;
}

/* A plain SELECT_2G4 is not enough after losing an A1 ACK: the CH592 may
 * still consider RF connected and emit RECONNECTING without a later CONNECTED
 * event. Force a real RF edge so QMK always receives a fresh CONNECTED. */
static bool reconnect_2g4(void) {
    static const uint8_t commands[] = {OC_COMMAND_SELECT_USB, OC_COMMAND_SELECT_2G4};

    if (!ocp_queue_release_then_controls(commands, sizeof(commands))) {
        return false;
    }

    selected_target   = OC_TARGET_2G4;
    reselect_deferred = false;
    return true;
}

static bool force_target(oc_target_t target) {
    bool queued = target == OC_TARGET_2G4 ? reconnect_2g4() : select_target(OC_TARGET_USB, true);

    if (!queued) {
        selected_target = OC_TARGET_UNKNOWN;
    }
    return queued;
}

static void reset_pending_operation(void) {
    pending_operation           = OC_OPERATION_NONE;
    operation_enqueued          = false;
    operation_cancelled         = false;
    operation_retry_deferred    = false;
    operation_retry_deferred_at = 0;
}

/* A frame already submitted to the UART cannot be recalled safely. Cancel its
 * retry intent, but keep the operation guard until the queued sequence either
 * receives its final ACK or aborts. The requested host is routed afterwards. */
static bool cancel_pending_operation(void) {
    if (pending_operation == OC_OPERATION_NONE) {
        return false;
    }

    reselect_deferred = false;
    if (operation_enqueued) {
        operation_cancelled      = true;
        operation_retry_deferred = false;
        return true;
    }

    reset_pending_operation();
    return false;
}

static void sync_target(void) {
    oc_target_t target = desired_target();

    if (!reselect_deferred) {
        if (target == OC_TARGET_2G4 && selected_target == OC_TARGET_UNKNOWN) {
            reconnect_2g4();
        } else {
            select_target(target, false);
        }
        return;
    }

    // A target change (notably USB becoming available in AUTO mode) must not
    // wait behind recovery for the failed target.
    if (target == reselect_target && timer_elapsed32(reselect_deferred_at) < OPENCONTROLLER_RESELECT_DELAY_MS) {
        return;
    }

    if (!force_target(target)) {
        reselect_deferred = true;
    }
}

static void select_auto(void) {
    bool wait_for_operation = cancel_pending_operation();

    connection_set_host(CONNECTION_HOST_AUTO);
    if (!wait_for_operation) {
        force_target(desired_target());
    }
}

static void select_usb(void) {
    bool wait_for_operation = cancel_pending_operation();

    connection_set_host_noeeprom(CONNECTION_HOST_USB);
    if (!wait_for_operation) {
        force_target(OC_TARGET_USB);
    }
}

static void select_2g4(void) {
    bool wait_for_operation = cancel_pending_operation();

    connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
    if (!wait_for_operation) {
        selected_target = OC_TARGET_UNKNOWN;
        force_target(OC_TARGET_2G4);
    }
}

static void pair_2g4(void) {
    if (pending_operation != OC_OPERATION_NONE) {
        return;
    }

    connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
    reselect_deferred        = false;
    pending_operation        = OC_OPERATION_PAIR;
    operation_enqueued       = false;
    operation_cancelled      = false;
    operation_retry_deferred = false;
}

static void unpair_2g4(void) {
    if (pending_operation != OC_OPERATION_NONE) {
        return;
    }

    connection_set_host(CONNECTION_HOST_AUTO);
    reselect_deferred        = false;
    pending_operation        = OC_OPERATION_UNPAIR;
    operation_enqueued       = false;
    operation_cancelled      = false;
    operation_retry_deferred = false;
}

static void service_pending_operation(void) {
    static const uint8_t pair_commands[]       = {OC_COMMAND_SELECT_2G4, OC_COMMAND_UNPAIR, OC_COMMAND_PAIR};
    static const uint8_t unpair_usb_commands[] = {OC_COMMAND_UNPAIR, OC_COMMAND_SELECT_USB};
    static const uint8_t unpair_2g4_commands[] = {OC_COMMAND_UNPAIR, OC_COMMAND_SELECT_USB, OC_COMMAND_SELECT_2G4};
    const uint8_t       *commands;
    uint8_t              command_count;
    oc_target_t          target;

    if (pending_operation == OC_OPERATION_NONE) {
        return;
    }

    if (operation_enqueued) {
        if (!ocp_actions_pending()) {
            reset_pending_operation();
        }
        return;
    }

    if (operation_retry_deferred && timer_elapsed32(operation_retry_deferred_at) < OPENCONTROLLER_RESELECT_DELAY_MS) {
        return;
    }

    if (pending_operation == OC_OPERATION_PAIR) {
        commands      = pair_commands;
        command_count = sizeof(pair_commands);
        target        = OC_TARGET_2G4;
    } else {
        target = desired_target();
        if (target == OC_TARGET_2G4) {
            commands      = unpair_2g4_commands;
            command_count = sizeof(unpair_2g4_commands);
        } else {
            commands      = unpair_usb_commands;
            command_count = sizeof(unpair_usb_commands);
        }
    }

    if (ocp_queue_available() < (uint8_t)(command_count + 1) || !ocp_queue_release_then_controls(commands, command_count)) {
        return;
    }

    selected_target          = target;
    operation_enqueued       = true;
    operation_retry_deferred = false;
}

void connection_host_changed_kb(connection_host_t host) {
    cancel_pending_operation();

    if (host == CONNECTION_HOST_2P4GHZ) {
        // QMK has no active host driver for CONNECTION_HOST_2P4GHZ yet. Use
        // its Bluetooth driver slot so host routing and the CH592 agree.
        connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
    }
}

static void serialize_keyboard_report(uint8_t output[OCP_KEYBOARD_REPORT_SIZE], const report_keyboard_t *report) {
    memset(output, 0, OCP_KEYBOARD_REPORT_SIZE);

    if (report != NULL) {
        output[0] = report->mods;
        output[1] = report->reserved;
        memcpy(&output[2], report->keys, KEYBOARD_REPORT_KEYS);
    }
}

static void begin_keyboard_resync(void) {
    uint8_t report[OCP_KEYBOARD_REPORT_SIZE];

    serialize_keyboard_report(report, keyboard_report);
    ocp_begin_keyboard_resync(report);
}

void bluetooth_init(void) {
    ocp_init();
    selected_target             = OC_TARGET_UNKNOWN;
    reselect_deferred           = false;
    reselect_deferred_at        = 0;
    reselect_target             = OC_TARGET_UNKNOWN;
    connection_generation       = 0;
    pending_operation           = OC_OPERATION_NONE;
    operation_enqueued          = false;
    operation_cancelled         = false;
    operation_retry_deferred    = false;
    operation_retry_deferred_at = 0;

    if (connection_get_host_raw() == CONNECTION_HOST_2P4GHZ) {
        // Repair a value persisted by native OU_2P4/QK_OUTPUT_2P4GHZ.
        connection_set_host(CONNECTION_HOST_BLUETOOTH);
    }

    // CHWAKE is not used by the MK65 OpenController profile.
    gpio_set_pin_input_low(A1);
    uart_init(OPENCONTROLLER_BAUD);
}

void bluetooth_task(void) {
    uint8_t  budget = OPENCONTROLLER_RX_BUDGET;
    uint32_t now_ms = timer_read32();

    if (connection_get_host_raw() == CONNECTION_HOST_2P4GHZ) {
        connection_set_host(CONNECTION_HOST_BLUETOOTH);
    }

    while (budget-- != 0 && uart_available()) {
        ocp_feed_byte(uart_read(), now_ms);
    }

    if (connection_generation != ocp_get_connection_generation()) {
        connection_generation = ocp_get_connection_generation();
        reselect_deferred     = false;
        if (desired_target() == OC_TARGET_2G4) {
            begin_keyboard_resync();
        }
    }

    if (pending_operation != OC_OPERATION_NONE) {
        service_pending_operation();
    }
    if (pending_operation == OC_OPERATION_NONE) {
        sync_target();
    }

    ocp_service(timer_read32(), opencontroller_uart_try_send, NULL);

    if (ocp_take_tx_failure()) {
        selected_target = OC_TARGET_UNKNOWN;
        if (pending_operation != OC_OPERATION_NONE) {
            if (operation_cancelled) {
                reset_pending_operation();
                reselect_target      = desired_target();
                reselect_deferred    = true;
                reselect_deferred_at = timer_read32();
            } else {
                operation_enqueued          = false;
                operation_retry_deferred    = true;
                operation_retry_deferred_at = timer_read32();
                reselect_deferred           = false;
            }
        } else {
            reselect_target      = desired_target();
            reselect_deferred    = true;
            reselect_deferred_at = timer_read32();
        }
    }
}

bool bluetooth_is_connected(void) {
    return ocp_get_link_state() == OCP_LINK_CONNECTED;
}

bool bluetooth_can_send_nkro(void) {
    return false;
}

uint8_t bluetooth_keyboard_leds(void) {
    return ocp_get_keyboard_leds();
}

void bluetooth_send_keyboard(report_keyboard_t *report) {
    uint8_t boot_report[OCP_KEYBOARD_REPORT_SIZE] = {0};

    serialize_keyboard_report(boot_report, report);
    ocp_set_keyboard_report(boot_report);
}

/* OpenController wireless v1 carries only 6KRO keyboard frames. Keeping the
 * remaining custom Bluetooth hooks as explicit no-ops prevents QMK from
 * generating protocol frames that the CH592 firmware cannot consume. */
void bluetooth_send_nkro(report_nkro_t *report) {
    (void)report;
}

void bluetooth_send_mouse(report_mouse_t *report) {
    (void)report;
}

void bluetooth_send_consumer(uint16_t usage) {
    (void)usage;
}

void bluetooth_send_system(uint16_t usage) {
    (void)usage;
}

void bluetooth_send_raw_hid(uint8_t *data, uint8_t length) {
    (void)data;
    (void)length;
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case OC_AUTO:
            if (record->event.pressed) {
                select_auto();
            }
            return false;
        case OC_USB:
            if (record->event.pressed) {
                select_usb();
            }
            return false;
        case OC_2G4:
            if (record->event.pressed) {
                select_2g4();
            }
            return false;
        case OC_PAIR:
            if (record->event.pressed) {
                pair_2g4();
            }
            return false;
        case OC_UNPAIR:
            if (record->event.pressed) {
                unpair_2g4();
            }
            return false;
        default:
            return process_record_user(keycode, record);
    }
}
