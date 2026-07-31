// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include_next <board.h>

// Generic crystal-less board: no ST-Link MCO feeding HSE in bypass mode.
#undef STM32_HSE_BYPASS

// Retarget the ST_NUCLEO64_U083RC board files at the STM32U073, which is an
// U083 without the LCD controller and AES peripheral.
#undef STM32U083xx
#define STM32U073xx
