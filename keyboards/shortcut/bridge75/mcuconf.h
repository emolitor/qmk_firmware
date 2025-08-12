// Copyright 2025 emolitor github.com/emolitor)
// Copyright 2024 Wind (@yelishang)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include_next <mcuconf.h>

#undef WB32_SERIAL_USE_UART1
#define WB32_SERIAL_USE_UART1 TRUE

#undef WB32_SPI_USE_QSPI
#define WB32_SPI_USE_QSPI TRUE

// The SysTick timer from the normal quantum/WB32 uses TIM2 -- the WS2812 pin used
// on the Bridge75 requires the use of TIM2 to run PWM -- rework which timers are
// allocated for PWM usage.
#undef WB32_PWM_USE_TIM2
#undef WB32_PWM_USE_TIM3
#define WB32_PWM_USE_TIM2 TRUE
#define WB32_PWM_USE_TIM3 FALSE

// As mentioned above, we need to reallocate the SysTick timer used from
// TIM2 to TIM3.
#undef WB32_ST_USE_TIMER
#define WB32_ST_USE_TIMER 3
