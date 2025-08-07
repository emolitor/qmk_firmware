// Copyright 2025 emolitor (github.com/emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Initialize power management system
void power_init(void);

// Trigger activity (resets idle timer)
void power_activity_trigger(void);

// Enter light sleep mode (CPU idle, peripherals active)
void power_enter_light_sleep(void);

// Enter deep sleep mode (most peripherals disabled)
void power_enter_deep_sleep(void);

// Exit deep sleep mode
void power_exit_deep_sleep(void);

// Main power management task (call from matrix_scan)
void power_task(void);

// Enable/disable power saving
void power_set_enabled(bool enabled);
bool power_is_enabled(void);

// Check if currently in deep sleep
bool power_is_sleeping(void);

// Matrix scan hook for activity detection
void matrix_scan_power(void);

// Power control register definitions for WB32FQ95
#define PWR_CR_CWUF    (1U << 2)  // Clear wake-up flag
#define PWR_CR_PDDS    (1U << 1)  // Power down deep sleep
#define PWR_CR_LPDS    (1U << 0)  // Low power deep sleep

// EXTI interrupt handlers
void EXTI9_5_IRQHandler(void);
void EXTI15_10_IRQHandler(void);