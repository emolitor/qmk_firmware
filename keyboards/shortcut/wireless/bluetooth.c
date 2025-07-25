
#include "quantum.h"
#include "bluetooth.h"
#include "wireless.h"

/*
 * Community Westberry wireless bluetooth support for QMK
 *
 * This code is derived from a substantially simplified version of the
 * westberry wireless code released from https://github.com/WestberryTech
 *
 * Prototype
 * The plan is to come up with a somwhat hacky version that just wraps the
 * existing implementation in wireless.c.
 *
 * M0
 * Refactor wireless.c away using the methods in module.c directly
 *
 * M1
 * Refactor module.c code away so Usmsg.c is used directly
 *
 * M2
 * Refactor smsg.c code away so QMK UART code is used directly
 *
 * M3
 * Add back in necessary sleep code
 */

// TODO: Remove after refactoring
void md_send_devinfo(const char *name);
uint8_t wireless_keyboard_leds(void);
void wireless_send_keyboard(report_keyboard_t *report);
void wireless_send_nkro(report_nkro_t *report);
void wireless_send_mouse(report_mouse_t *report);
void wireless_send_extra(report_extra_t *report);

void bluetooth_init(void) {
    //wireless_init();
    md_init();
    md_send_devinfo(MD_BT_NAME);
    wait_ms(10);
}

void bluetooth_task(void) {
    wireless_task(); // TODO Refactor
}

bool bluetooth_is_connected(void) {
    return *md_getp_state() == MD_STATE_CONNECTED;
}

bool bluetooth_can_send_nkro(void) {
    return true;
}

uint8_t bluetooth_keyboard_leds(void) {
    if (bluetooth_is_connected()) {
        return *md_getp_indicator();
    }

    return 0;
}

void bluetooth_send_keyboard(report_keyboard_t *report) {
    wireless_send_keyboard(report); // TODO Refactor
}

void bluetooth_send_nkro(report_nkro_t *report) {
    wireless_send_nkro(report); // TODO Refactor
}

void bluetooth_send_mouse(report_mouse_t *report) {
    wireless_send_mouse(report); // TODO Refactor
}
