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

#include "quantum.h"
#include "bluetooth.h"
#include "lowpower.h"
#include "module.h"
#include "smsg.h"
#include "transport.h"

#ifndef WLS_INQUIRY_BAT_TIME
#    define WLS_INQUIRY_BAT_TIME 3000
#endif

// TODO: Remove after refactoring
void    md_send_devinfo(const char *name);
//uint8_t wireless_keyboard_leds(void);
//void    wireless_send_keyboard(report_keyboard_t *report);
//void    wireless_send_nkro(report_nkro_t *report);
//void    wireless_send_mouse(report_mouse_t *report);
//void    wireless_send_extra(report_extra_t *report);

void bluetooth_init(void) {
    // wireless_init();
    md_init();
    md_send_devinfo(MD_BT_NAME);
    wait_ms(10);
}

void bluetooth_task(void) {
    // wireless_task(); // TODO Refactor
    //wireless_pre_task();
    lpwr_task();
    md_main_task();
    //wireless_post_task();

    /* usb_remote_wakeup() should be invoked last so that we have chance
     * to switch to wireless after start-up when usb is not connected
     */
    if (get_transport() == TRANSPORT_USB) {
        usb_remote_wakeup();
    } else if (lpwr_get_state() == LPWR_NORMAL) {
        static uint32_t inqtimer = 0x00;

        if (sync_timer_elapsed32(inqtimer) >= (WLS_INQUIRY_BAT_TIME)) {
            if (md_inquire_bat()) {
                inqtimer = sync_timer_read32();
            }
        }
    }
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
    // wireless_send_keyboard(report); // TODO Refactor
    if (MD_STATE_PAIRING == *md_getp_state()) {
        return;
    }
    uint8_t wls_report_kb[MD_SND_CMD_KB_LEN] = {0};

    // if (*md_getp_state() != MD_STATE_CONNECTED) {
    //     wireless_devs_change(wls_devs, wls_devs, false);
    //     return;
    // }

    if (report != NULL) {
        memcpy(wls_report_kb, (uint8_t *)report, sizeof(wls_report_kb));
    }
    md_send_kb(wls_report_kb);
}

void bluetooth_send_nkro(report_nkro_t *report) {
    // wireless_send_nkro(report); // TODO Refactor
    static report_keyboard_t temp_report_keyboard                 = {0};
    uint8_t                  wls_report_nkro[MD_SND_CMD_NKRO_LEN] = {0};

    if (MD_STATE_PAIRING == *md_getp_state()) {
        return;
    }

    // if (*md_getp_state() != MD_STATE_CONNECTED) {
    //     wireless_devs_change(wls_devs, wls_devs, false);
    //     return;
    // }

    if (report != NULL) {
        report_nkro_t temp_report_nkro = *report;
        uint8_t       key_count        = 0;

        temp_report_keyboard.mods = temp_report_nkro.mods;
        for (uint8_t i = 0; i < NKRO_REPORT_BITS; i++) {
            key_count += __builtin_popcount(temp_report_nkro.bits[i]);
        }

        // find key up and del it.
        for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS && temp_report_keyboard.keys[i]; i++) {
            uint8_t usageid = 0x00;
            uint8_t n;

            for (uint8_t c = 0; c < key_count; c++) {
                for (n = 0; n < NKRO_REPORT_BITS && !temp_report_nkro.bits[n]; n++) {
                }
                usageid = (n << 3) | biton(temp_report_nkro.bits[n]);
#ifdef NKRO_ENABLE
                del_key_bit(&temp_report_nkro, usageid);
#endif
                if (usageid == temp_report_keyboard.keys[i]) {
                    break;
                }
            }

            if (usageid != temp_report_keyboard.keys[i]) {
                temp_report_keyboard.keys[i] = 0x00;
            }
        }

        /*
         * Use NKRO for sending when more than 6 keys are pressed
         * to solve the issue of the lack of a protocol flag in wireless mode.
         */

        temp_report_nkro = *report;

        for (uint8_t i = 0; i < key_count; i++) {
            uint8_t usageid;
            uint8_t idx, n = 0;

            for (n = 0; n < NKRO_REPORT_BITS && !temp_report_nkro.bits[n]; n++) {
            }
            usageid = (n << 3) | biton(temp_report_nkro.bits[n]);
#ifdef NKRO_ENABLE
            del_key_bit(&temp_report_nkro, usageid);
#endif

            for (idx = 0; idx < KEYBOARD_REPORT_KEYS; idx++) {
                if (temp_report_keyboard.keys[idx] == usageid) {
                    break;
                }
                if (temp_report_keyboard.keys[idx] == 0x00) {
                    temp_report_keyboard.keys[idx] = usageid;
                    break;
                }
            }

            if (idx == KEYBOARD_REPORT_KEYS && (usageid < (MD_SND_CMD_NKRO_LEN * 8))) {
                wls_report_nkro[usageid / 8] |= 0x01 << (usageid % 8);
            }
        }
    } else {
        memset(&temp_report_keyboard, 0, sizeof(temp_report_keyboard));
    }

    // wireless_driver.send_keyboard(&temp_report_keyboard);
    while (smsg_is_busy()) {
        //wireless_task();
        bluetooth_task();
    }
    host_keyboard_send(&temp_report_keyboard);
    md_send_nkro(wls_report_nkro);
}

void bluetooth_send_mouse(report_mouse_t *report) {
    // wireless_send_mouse(report); // TODO Refactor
    if (MD_STATE_PAIRING == *md_getp_state()) {
        return;
    }

    typedef struct {
        uint8_t buttons;
        int8_t  x;
        int8_t  y;
        int8_t  z;
        int8_t  h;
    } __attribute__((packed)) wls_report_mouse_t;

    wls_report_mouse_t wls_report_mouse = {0};

    // if (*md_getp_state() != MD_STATE_CONNECTED) {
    //     wireless_devs_change(wls_devs, wls_devs, false);
    //     return;
    // }

    if (report != NULL) {
        wls_report_mouse.buttons = report->buttons;
        wls_report_mouse.x       = report->x;
        wls_report_mouse.y       = report->y;
        wls_report_mouse.z       = report->h;
        wls_report_mouse.h       = report->v;
    }

    md_send_mouse((uint8_t *)&wls_report_mouse);
}
