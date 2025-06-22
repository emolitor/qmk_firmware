// Copyright 2025 emolitor github.com/emolitor)
// Copyright 2024 Wind (@yelishang)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include_next <mcuconf.h>

#undef WB32_SERIAL_USE_UART1
#define WB32_SERIAL_USE_UART1 TRUE

#undef WB32_SPI_USE_QSPI
#define WB32_SPI_USE_QSPI TRUE

#undef WB32_LSI_ENABLED
#define WB32_LSI_ENABLED TRUE

#undef WB32_EXTI_REQUIRED
#define WB32_EXTI_REQUIRED TRUE
