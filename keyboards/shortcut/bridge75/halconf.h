// Copyright 2025 emolitor (github.com/emolitor)
// Copyright 2024 Wind (@yelishang)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Required for UART wireless communication
#define HAL_USE_SERIAL TRUE

// Required for SPI flash (EEPROM wear leveling)
#define HAL_USE_SPI TRUE
#define SPI_USE_WAIT TRUE
#define SPI_SELECT_MODE SPI_SELECT_MODE_PAD

// Required for WS2812 GPIO DMA timer (TIM3)
#define HAL_USE_GPT TRUE

// Required for low-power sleep wakeup callbacks
#define PAL_USE_CALLBACKS TRUE

#include_next <halconf.h>
