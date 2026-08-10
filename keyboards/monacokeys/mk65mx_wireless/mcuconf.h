// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include_next <mcuconf.h>

#undef STM32_I2C_USE_I2C1
#define STM32_I2C_USE_I2C1 TRUE

#ifdef OPENCONTROLLER_ENABLE
#    undef STM32_SERIAL_USE_USART2
#    define STM32_SERIAL_USE_USART2 TRUE
#endif
