// Copyright 2025 emolitor (github.com/emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "power.h"
#include "westberry/wb_bluetooth.h"
#include <hal.h>

// Timer for idle detection
static uint32_t last_activity_time = 0;
static bool power_save_enabled = true;
static bool in_deep_sleep = false;

// Power saving configuration
#ifndef POWER_IDLE_TIMEOUT_MS
#define POWER_IDLE_TIMEOUT_MS 30000        // 30 seconds before light sleep
#endif
#ifndef POWER_DEEP_SLEEP_TIMEOUT_MS
#define POWER_DEEP_SLEEP_TIMEOUT_MS 300000 // 5 minutes before deep sleep
#endif
#define ACTIVITY_CHECK_INTERVAL_MS 1000 // Check every second

// UART state preservation
static bool uart_was_active = false;

void power_init(void) {
    last_activity_time = timer_read32();
    power_save_enabled = true;
    in_deep_sleep = false;
}

void power_activity_trigger(void) {
    last_activity_time = timer_read32();
    
    // Wake from deep sleep if needed
    if (in_deep_sleep) {
        power_exit_deep_sleep();
    }
}

static void save_uart_state(void) {
    // Save UART configuration for restoration after wake
    #if WB32_SERIAL_USE_UART1
        uart_was_active = (SD1.state == SD_READY);
    #endif
}

static void restore_uart_state(void) {
    // Restore UART for wireless communication
    #if WB32_SERIAL_USE_UART1
    if (uart_was_active) {
        // Re-initialize UART with default configuration
        // The serial driver will be reinitialized by the wireless module
        
        // Give the wireless module time to stabilize
        wait_ms(10);
        
        // Re-establish wireless connection
        wireless_devs_change(wireless_get_current_devs(), wireless_get_current_devs(), false);
    }
    #endif
}

static void configure_wakeup_sources(void) {
    // Configure GPIO pins for wake-up capability
    // Matrix rows: B15, C6, C7, C8, C9, B14
    
    // Configure rows as input with pull-up for wake detection
    gpio_set_pin_input_high(B15);
    gpio_set_pin_input_high(C6);
    gpio_set_pin_input_high(C7);
    gpio_set_pin_input_high(C8);
    gpio_set_pin_input_high(C9);
    gpio_set_pin_input_high(B14);
    
    // Enable EXTI for the pins that support it
    // B15 = EXTI line 15, B14 = EXTI line 14, C6-C9 = EXTI lines 6-9
    nvicEnableVector(EXTI15_10_IRQn, WB32_IRQ_EXTI10_15_PRIORITY);
    nvicEnableVector(EXTI9_5_IRQn, WB32_IRQ_EXTI5_9_PRIORITY);
    
    // Configure EXTI lines for falling edge detection (key press)
    // Enable for B15, B14 (lines 15, 14)
    EXTI->FTSR |= (1U << 15) | (1U << 14);
    EXTI->IMR |= (1U << 15) | (1U << 14);
    
    // Enable for C6-C9 (lines 6-9)
    EXTI->FTSR |= (0xF << 6); // Lines 6,7,8,9
    EXTI->IMR |= (0xF << 6);
}

void power_enter_light_sleep(void) {
    // Light sleep - CPU idle but peripherals active
    // UART remains active for wireless communication
    
    // Reduce system clock if possible
    // Note: Be careful not to affect UART baud rate
    
    // Enter WFI (Wait For Interrupt) mode
    __WFI();
}

void power_enter_deep_sleep(void) {
    if (in_deep_sleep) return;
    
    in_deep_sleep = true;
    
    // Notify user we're going to sleep (optional LED indication)
    gpio_write_pin_high(LED_POWER_EN_PIN);
    
    // Save peripheral states
    save_uart_state();
    
    // Configure wake-up sources
    configure_wakeup_sources();
    
    // Disable unnecessary peripherals
    // Keep RTC and EXTI active for wake-up
    
    // Stop UART to save power (will be restored on wake)
    #if WB32_SERIAL_USE_UART1
    if (uart_was_active) {
        sdStop(&SD1);
    }
    #endif
    
    // Configure PWR control register for deep sleep
    PWR->CR0 |= PWR_CR_PDDS; // Power down deep sleep
    PWR->CR0 |= PWR_CR_LPDS; // Low power deep sleep
    
    // Clear wake-up flag
    PWR->CR0 |= PWR_CR_CWUF;
    
    // Set SLEEPDEEP bit
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    
    // Enter stop mode (deep sleep)
    __WFI();
    
    // CPU resumes here after wake-up
    
    // Clear SLEEPDEEP bit
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
}

void power_exit_deep_sleep(void) {
    if (!in_deep_sleep) return;
    
    in_deep_sleep = false;
    
    // Restore system clock
    hal_lld_init();
    
    // Restore peripheral states
    restore_uart_state();
    
    // Re-enable LED power
    gpio_write_pin_low(LED_POWER_EN_PIN);
    
    // Restore RGB matrix if it was enabled
    rgb_matrix_reload_from_eeprom();
    
    // Reset activity timer
    last_activity_time = timer_read32();
}

void power_task(void) {
    if (!power_save_enabled) return;
    
    static uint32_t last_check = 0;
    uint32_t now = timer_read32();
    
    // Only check periodically to reduce overhead
    if (TIMER_DIFF_32(now, last_check) < ACTIVITY_CHECK_INTERVAL_MS) {
        return;
    }
    last_check = now;
    
    uint32_t idle_time = TIMER_DIFF_32(now, last_activity_time);
    
    // Check if we should enter deep sleep
    if (idle_time > POWER_DEEP_SLEEP_TIMEOUT_MS && !in_deep_sleep) {
        // Only enter deep sleep in wireless modes
        if (wireless_get_current_devs() != DEVS_USB) {
            power_enter_deep_sleep();
        }
    }
    // Check if we should enter light sleep
    else if (idle_time > POWER_IDLE_TIMEOUT_MS && !in_deep_sleep) {
        power_enter_light_sleep();
    }
}

void power_set_enabled(bool enabled) {
    power_save_enabled = enabled;
    if (enabled) {
        last_activity_time = timer_read32();
    }
}

bool power_is_enabled(void) {
    return power_save_enabled;
}

bool power_is_sleeping(void) {
    return in_deep_sleep;
}

// Matrix scan hook to detect activity
void matrix_scan_power(void) {
    static uint16_t prev_matrix[MATRIX_ROWS] = {0};
    bool activity_detected = false;
    
    // Check for any key press activity
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        uint16_t current = matrix_get_row(row);
        if (current != prev_matrix[row]) {
            activity_detected = true;
            prev_matrix[row] = current;
        }
    }
    
    if (activity_detected) {
        power_activity_trigger();
    }
}

// EXTI interrupt handlers for wake-up
void EXTI9_5_IRQHandler(void) {
    // Check and clear pending bits for lines 6-9 (C6-C9)
    uint32_t pending = EXTI->PR & 0x3C0; // Mask for lines 6-9
    EXTI->PR = pending;
    if (pending) {
        power_activity_trigger();
    }
}

void EXTI15_10_IRQHandler(void) {
    // Check and clear pending bits for lines 14-15 (B14, B15)
    uint32_t pending = EXTI->PR & 0xC000; // Mask for lines 14-15
    EXTI->PR = pending;
    if (pending) {
        power_activity_trigger();
    }
}