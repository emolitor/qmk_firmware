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

#ifdef OPENBOOT_BRIDGE_ENABLE
#    include "openboot_bridge.h"
#    include "openboot_hid.h"
#endif

#define OPENCONTROLLER_BAUD 115200
#define OPENCONTROLLER_RX_BUDGET 32
#define OPENCONTROLLER_RESELECT_DELAY_MS 1000

/* Bench builds can watch the wire: a keyboard that defines
 * OPENCONTROLLER_TRACE provides these and sees every frame handed to the UART
 * and every byte taken from it. Production builds compile them away. */
#ifdef OPENCONTROLLER_TRACE
void opencontroller_trace_tx(const uint8_t *data, uint8_t length);
void opencontroller_trace_rx(uint8_t byte);
#    define OC_TRACE_TX(data, length) opencontroller_trace_tx((data), (length))
#    define OC_TRACE_RX(byte) opencontroller_trace_rx(byte)
#else
#    define OC_TRACE_TX(data, length) ((void)0)
#    define OC_TRACE_RX(byte) ((void)0)
#endif

/*
 * Idle time on the 2.4 GHz transport after which the module is told to tear
 * its link down and deep-sleep. The next key press wakes it; expect the bonded
 * reconnect to take one to two seconds. Zero disables the timeout; OC_SLEEP
 * still works. Auto-sleep (the module sleeping by itself whenever its radio
 * is idle or searching) is independent of this and on by default.
 */
#ifndef OPENCONTROLLER_SLEEP_TIMEOUT_MS
#    define OPENCONTROLLER_SLEEP_TIMEOUT_MS 600000
#endif

#define OC_COMMAND_SELECT_USB 0x11
#define OC_COMMAND_SELECT_2G4 0x30
#define OC_COMMAND_PAIR 0x51
#define OC_COMMAND_UNPAIR 0x52
/* Acknowledged, then the module reboots into the OpenBoot bootloader. */
#define OC_COMMAND_OTA 0x81

/*
 * Ceiling on any single drain of the UART, in bytes. A CH592 held in reset or
 * unpowered leaves its TX line low, which the USART reports as an unbroken run
 * of framing errors - so an unbounded "drain until empty" loop never finds an
 * empty queue, spins the main loop forever, and takes USB down with it. One
 * full serial queue per pass still empties the link an order of magnitude
 * faster than 115200 can fill it.
 */
#define OPENBOOT_BRIDGE_DRAIN_LIMIT 256

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
static bool           sleep_now_requested;  /* OC_SLEEP pressed */
static bool           sleep_wake_requested; /* a key was pressed while the module sleeps */
static uint32_t       link_requested_at;    /* last time the 2.4 GHz link was asked for */

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

    if (written == length) {
        OC_TRACE_TX(data, length);
    }
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

/* Asking for the radio restarts the idle clock: a link that has just been
 * requested is not idle, whatever the matrix has been doing. */
static void note_selected_target(oc_target_t target) {
    selected_target = target;
    if (target == OC_TARGET_2G4) {
        link_requested_at = timer_read32();
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

    note_selected_target(target);
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

    note_selected_target(OC_TARGET_2G4);
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

/* A routing or pairing key is a later, more specific intent: it must not be
 * followed by a sleep that was asked for earlier and got deferred. */
static void cancel_sleep_requests(void) {
    sleep_now_requested  = false;
    sleep_wake_requested = false;
}

static void select_auto(void) {
    bool wait_for_operation = cancel_pending_operation();

    cancel_sleep_requests();

    connection_set_host(CONNECTION_HOST_AUTO);
    if (!wait_for_operation) {
        force_target(desired_target());
    }
}

static void select_usb(void) {
    bool wait_for_operation = cancel_pending_operation();

    cancel_sleep_requests();

    connection_set_host_noeeprom(CONNECTION_HOST_USB);
    if (!wait_for_operation) {
        force_target(OC_TARGET_USB);
    }
}

static void select_2g4(void) {
    bool wait_for_operation = cancel_pending_operation();

    cancel_sleep_requests();

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

    cancel_sleep_requests();

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

    cancel_sleep_requests();

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

    note_selected_target(target);
    operation_enqueued       = true;
    operation_retry_deferred = false;
}

static bool autosleep_wanted(void) {
#ifdef OPENCONTROLLER_AUTOSLEEP_DISABLE
    return false;
#else
    return true;
#endif
}

static bool sleep_timeout_expired(void) {
#if OPENCONTROLLER_SLEEP_TIMEOUT_MS > 0
    return last_input_activity_elapsed() >= OPENCONTROLLER_SLEEP_TIMEOUT_MS && timer_elapsed32(link_requested_at) >= OPENCONTROLLER_SLEEP_TIMEOUT_MS;
#else
    return false;
#endif
}

static void request_sleep(void) {
    // Consumed when the frame is queued; validated against the transport there.
    if (ocp_get_module_sleep_state() == OCP_MODULE_AWAKE) {
        sleep_now_requested = true;
    }
}


/* Presses wake the module, releases do not: the OC_SLEEP chord is still being
 * let go while the sleep frame is on the wire. */
static void note_key_press(void) {
    if (ocp_get_module_sleep_state() != OCP_MODULE_AWAKE) {
        sleep_wake_requested = true;
    }
}

/*
 * Keeps the module's sleep state in step with policy. Called only while no
 * transport recovery or pairing operation is in progress, so the frames it
 * queues never sit in front of a select, and at most one frame per call.
 */
static void service_sleep(void) {
    switch (ocp_get_sleep_capability()) {
        case OCP_SLEEP_CAP_UNKNOWN:
            if (ocp_queue_available() != 0) {
                ocp_queue_sleep_negotiate();
            }
            return;
        case OCP_SLEEP_CAP_PENDING:
            return;
        case OCP_SLEEP_CAP_UNSUPPORTED:
            sleep_now_requested  = false;
            sleep_wake_requested = false;
            return;
        case OCP_SLEEP_CAP_READY:
            break;
    }

    switch (ocp_get_module_sleep_state()) {
        case OCP_MODULE_SLEEP_REQUESTED:
            // Let the sleep complete; the wake is serviced once it has.
            return;
        case OCP_MODULE_ASLEEP:
            if (sleep_wake_requested) {
                sleep_wake_requested = false;
                // Reselecting from scratch reuses the forced-RF-edge reconnect.
                // The protocol layer puts the wake preamble in front of it.
                if (desired_target() == OC_TARGET_2G4) {
                    selected_target = OC_TARGET_UNKNOWN;
                }
            }
            return;
        case OCP_MODULE_AWAKE:
            sleep_wake_requested = false;
            break;
    }

    if (ocp_queue_available() == 0) {
        return;
    }

    if (autosleep_wanted()) {
        if (ocp_get_autosleep_state() == OCP_AUTOSLEEP_OFF) {
            ocp_queue_autosleep_arm();
            return;
        }
    } else if (ocp_get_autosleep_state() == OCP_AUTOSLEEP_ARMED) {
        // Re-sending the unlock is the module's only auto-sleep disable path.
        ocp_queue_sleep_negotiate();
        return;
    }

    // Explicit sleep only makes sense on the radio. With USB selected the
    // module is already idle and auto-sleep covers it.
    if (desired_target() != OC_TARGET_2G4 || selected_target != OC_TARGET_2G4) {
        sleep_now_requested = false;
        return;
    }
    if (!sleep_now_requested && !sleep_timeout_expired()) {
        return;
    }
    // A key still held on the host would stay held across the torn-down link.
    if (has_anykey() || has_anymod()) {
        return;
    }
    if (ocp_actions_pending() || ocp_resync_is_active() || ocp_queue_available() < 2) {
        return;
    }
    if (ocp_queue_sleep_now()) {
        sleep_now_requested = false;
    }
}

#ifdef OPENBOOT_BRIDGE_ENABLE

static bool             bridge_busy;
static bool             bridge_ota_inflight;
static connection_host_t bridge_saved_host;

/* True from the moment ENTER is accepted until the link has been handed back.
 * The error state is deliberately excluded: the bridge owns nothing there, so
 * the keyboard's transport keycodes must keep working. */
static bool bridge_is_active(void) {
    obb_state_t state = obb_get_state();

    return bridge_busy || (state != OBB_STATE_IDLE && state != OBB_STATE_ERROR);
}

static bool bridge_host_try_send(const uint8_t *data, uint8_t length, void *context) {
    (void)length;
    (void)context;

    return openboot_usb_send(data);
}

static void bridge_seize(void) {
    bridge_busy       = true;
    bridge_saved_host = connection_get_host_raw();

    /* While the module is off air QMK would route every report to the wireless
     * driver, leaving the user with a dead keyboard for the whole update. */
    connection_set_host_noeeprom(CONNECTION_HOST_USB);

#ifdef RGB_MATRIX_ENABLE
    /* Not cosmetic: a matrix flush blocks the main loop on I2C for long enough
     * to overflow the serial input queue mid-transfer. */
    rgb_matrix_disable_noeeprom();
#endif
}

static void bridge_release(void) {
    uint16_t drained = OPENBOOT_BRIDGE_DRAIN_LIMIT;

    while (drained-- != 0 && uart_available()) {
        (void)uart_read();
    }

    /* This is the state bluetooth_init() leaves behind, minus uart_init(), so
     * the existing sync_target() path does the reconnect on the next call. */
    ocp_init();
    selected_target       = OC_TARGET_UNKNOWN;
    reselect_deferred     = false;
    reselect_target       = OC_TARGET_UNKNOWN;
    connection_generation = ocp_get_connection_generation();
    reset_pending_operation();
    sleep_now_requested  = false;
    sleep_wake_requested = false;
    link_requested_at    = timer_read32();

    connection_set_host_noeeprom(bridge_saved_host);

#ifdef RGB_MATRIX_ENABLE
    rgb_matrix_enable_noeeprom();
#endif

    bridge_ota_inflight = false;
    bridge_busy         = false;
}

/* Returns true while the bridge, and not the OpenController protocol, owns the
 * UART. Keeping this inside bluetooth_task() is what makes "exactly one UART
 * consumer per iteration" a property of the control flow. */
static bool openboot_bridge_task(uint32_t now_ms) {
    uint8_t report[OBB_REPORT_SIZE];
    uint8_t chunk[OBB_MAX_DATA];
    uint8_t count;

    if (!usb_connected_state()) {
        obb_on_usb_inactive(now_ms);
    }

    while (obb_can_accept_host_report() && openboot_usb_receive(report)) {
        obb_on_host_report(report, (uint8_t)sizeof(report), now_ms);
    }

    obb_set_ocp_state(ocp_is_idle(), (uint8_t)ocp_get_link_state());

    if (obb_take_ota_request()) {
        if (ocp_queue_control(OC_COMMAND_OTA)) {
            bridge_ota_inflight = true;
        } else {
            obb_on_ota_failed(now_ms);
        }
    }

    /* The queued command is last in the FIFO, so the queue draining is the
     * acknowledgement. A failure arrives instead through ocp_take_tx_failure(). */
    if (bridge_ota_inflight && !ocp_actions_pending()) {
        bridge_ota_inflight = false;
        obb_on_ota_acked(now_ms);
    }

    if (obb_owns_uart()) {
        /* Bounded for the same reason as bridge_release(): while settling, the
         * bridge accepts bytes unconditionally in order to discard them, so a
         * line stuck low would keep this loop fed indefinitely. */
        uint16_t budget = OPENBOOT_BRIDGE_DRAIN_LIMIT;

        while (budget != 0 && obb_can_accept_uart_bytes() && uart_available()) {
            count = 0;
            while (count < (uint8_t)sizeof(chunk) && budget != 0 && uart_available()) {
                chunk[count++] = uart_read();
                budget--;
            }
            obb_on_uart_bytes(chunk, count, now_ms);
        }
    }

    obb_service(now_ms, opencontroller_uart_try_send, bridge_host_try_send, NULL);

    if (obb_take_seize_request()) {
        bridge_seize();
    }
    if (obb_take_release_request()) {
        bridge_release();
    }

    return obb_owns_uart();
}

static bool bridge_take_tx_failure(uint32_t now_ms) {
    if (!bridge_ota_inflight) {
        return false;
    }

    bridge_ota_inflight = false;
    obb_on_ota_failed(now_ms);
    return true;
}

#else

static inline bool bridge_is_active(void) {
    return false;
}

static inline bool bridge_take_tx_failure(uint32_t now_ms) {
    (void)now_ms;
    return false;
}

#endif

void connection_host_changed_kb(connection_host_t host) {
    if (bridge_is_active()) {
        // The bridge pins the host itself; cancelling here would be meaningless.
        return;
    }

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
#ifdef OPENBOOT_BRIDGE_ENABLE
    obb_init();
    bridge_busy         = false;
    bridge_ota_inflight = false;
#endif
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
    sleep_now_requested         = false;
    sleep_wake_requested        = false;
    link_requested_at           = timer_read32();

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

#ifdef OPENBOOT_BRIDGE_ENABLE
    if (openboot_bridge_task(now_ms)) {
        // The bridge owns the UART this iteration.
        return;
    }
#endif

    if (connection_get_host_raw() == CONNECTION_HOST_2P4GHZ) {
        connection_set_host(CONNECTION_HOST_BLUETOOTH);
    }

    while (budget-- != 0 && uart_available()) {
        uint8_t byte = uart_read();

        OC_TRACE_RX(byte);
        ocp_feed_byte(byte, now_ms);
    }

    if (connection_generation != ocp_get_connection_generation()) {
        connection_generation = ocp_get_connection_generation();
        reselect_deferred     = false;
        if (desired_target() == OC_TARGET_2G4) {
            begin_keyboard_resync();
        }
    }

    // Nothing new may be queued while the bridge is waiting for its own control
    // frame to be acknowledged; a select would sit in front of it.
    if (!bridge_is_active()) {
        if (pending_operation != OC_OPERATION_NONE) {
            service_pending_operation();
        }
        if (pending_operation == OC_OPERATION_NONE) {
            sync_target();
            // Not during recovery: a failed transport must keep its backoff
            // instead of being prodded with a sleep negotiation every pass.
            if (!reselect_deferred) {
                service_sleep();
            }
        }
    }

    ocp_service(timer_read32(), opencontroller_uart_try_send, NULL);

    if (ocp_take_tx_failure()) {
        bridge_take_tx_failure(timer_read32());
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
    if (bridge_is_active()) {
        // These all queue control frames onto a UART the bridge owns.
        switch (keycode) {
            case OC_AUTO:
            case OC_USB:
            case OC_2G4:
            case OC_PAIR:
            case OC_UNPAIR:
            case OC_SLEEP:
                return false;
            default:
                break;
        }
    }

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
        case OC_SLEEP:
            if (record->event.pressed) {
                request_sleep();
            }
            return false;
        default:
            // The routing keys above wake the module through the frames they
            // queue; everything else wakes it by asking for the link back.
            if (record->event.pressed) {
                note_key_press();
            }
            return process_record_user(keycode, record);
    }
}
