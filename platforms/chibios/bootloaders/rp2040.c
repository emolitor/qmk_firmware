// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hal.h"
#include "bootloader.h"
#include "gpio.h"
#include "wait.h"

#if !defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED)
#    define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK 0U
#else
#    define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK (1U << RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED)
#endif

__attribute__((weak)) void mcu_reset(void) {
    NVIC_SystemReset();
}
void bootloader_jump(void) {
    rpRomResetUsbBoot(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK, 0U);
}

void enter_bootloader_mode_if_requested(void) {}

#if defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET)
#    if !defined(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT)
#        define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 200U
#    endif

// Needs to be located in a RAM section that is never initialized on boot to
// preserve its value on reset
static volatile uint32_t __attribute__((section(".ram0.bootloader_magic"))) magic_location;
const uint32_t                                                              magic_token = 0xCAFEB0BA;

// We can not use the __early_init / enter_bootloader_mode_if_requested hook as
// we depend on an already initialized system with usable memory regions. This
// function is called just prior to main. Clocks (and the watchdog tick that
// feeds the TIMER peripheral) are already up: the ChibiOS board file runs
// rp_clock_init() from __early_init.
void __late_init(void) {
    if (magic_location != magic_token) {
        magic_location = magic_token;
        // ChibiOS is not initialized at this point, so sleeping is only
        // possible via busy waiting on the TIMER peripheral, which has to be
        // released from reset first. wait_us() takes a uint16_t, so wait in
        // millisecond steps.
        rp_peripheral_unreset(RESETS_ALLREG_TIMER0);
        for (uint32_t ms = RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT; ms > 0U; ms--) {
            wait_us(1000U);
        }
        magic_location = 0;
        return;
    }

    magic_location = 0;
    rpRomResetUsbBoot(RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK, 0U);
}

#endif
