// Copyright 2026 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hal.h"
#include "bootloader.h"
#include "gpio.h"

#if !defined(RP2350_BOOTLOADER_DOUBLE_TAP_RESET_LED)
#    define RP2350_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK 0U
#else
#    define RP2350_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK (1U << RP2350_BOOTLOADER_DOUBLE_TAP_RESET_LED)
#endif

__attribute__((weak)) void mcu_reset(void) {
#if defined(__riscv)
    // Hazard3 has no NVIC system-reset; use the bootrom reboot API.
    rpRomReboot(RP_ROM_REBOOT2_FLAG_REBOOT_TYPE_NORMAL | RP_ROM_REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 1U, 0U, 0U);
    while (true) {
    }
#else
    NVIC_SystemReset();
#endif
}

void bootloader_jump(void) {
    // Enters the BOOTSEL USB bootloader; works in both ARM and RISC-V mode.
    rpRomResetUsbBoot(RP2350_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK, 0U);
}

void enter_bootloader_mode_if_requested(void) {}

#if defined(RP2350_BOOTLOADER_DOUBLE_TAP_RESET)
#    if !defined(RP2350_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT)
#        define RP2350_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 200U
#    endif

// The watchdog scratch registers survive a warm reset on both architectures.
// SCRATCH[7] is safe for our token: the bootrom's watchdog-boot protocol only
// reacts to its own 0xb007c0d3 magic, which is never written here. (The
// RP2040 port keeps its token in an uninitialized RAM section instead, but
// the stock RP2350 RISC-V linker script has no such section.)
#    define BOOTLOADER_MAGIC_TOKEN 0xCAFEB0BAu

// Called by the crt0 of both architectures after data/bss initialization,
// just prior to main. Clocks are already up (the ChibiOS board file runs
// rp_clock_init() from __early_init), but no timer is running yet -- neither
// TIMER0 (started in hal_lld_init) nor the RISC-V machine timer (enabled in
// the Hazard3 port initialization). Delay via a crude busy loop calibrated
// against the 150MHz system clock instead.
void __late_init(void) {
    if (WATCHDOG->SCRATCH[7] != BOOTLOADER_MAGIC_TOKEN) {
        WATCHDOG->SCRATCH[7] = BOOTLOADER_MAGIC_TOKEN;
        // ~3 cycles per iteration.
        for (uint32_t i = RP2350_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT * (150000U / 3U); i > 0U; i--) {
            __asm__ volatile("" ::: "memory");
        }
        WATCHDOG->SCRATCH[7] = 0U;
        return;
    }

    WATCHDOG->SCRATCH[7] = 0U;
    rpRomResetUsbBoot(RP2350_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK, 0U);
}

#endif
