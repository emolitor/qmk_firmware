// Copyright 2025 emolitor github.com/emolitor)
// Copyright 2024 Wind (@yelishang)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include_next <mcuconf.h>

#undef WB32_SERIAL_USE_UART1
#define WB32_SERIAL_USE_UART1 TRUE

#undef WB32_SPI_USE_QSPI
#define WB32_SPI_USE_QSPI TRUE

// Interrupt priorities for wireless module communication stability
#undef WB32_UART_UART1_IRQ_PRIORITY
#define WB32_UART_UART1_IRQ_PRIORITY    8

#undef WB32_QSPI_IRQ_PRIORITY
#define WB32_QSPI_IRQ_PRIORITY          10

// WS2812 GPIO DMA driver uses TIM3 for pacing DMA transfers
#undef WB32_GPT_USE_TIM3
#define WB32_GPT_USE_TIM3 TRUE

// WS2812 GPIO DMA driver uses DMA directly (not through a HAL driver)
#define WB32_DMA_REQUIRED

// Improved deep sleep bug fix for Bridge75
//#undef LPWR_UART_WAKEUP_DISABLE
//#define LPWR_UART_WAKEUP_DISABLE TRUE

