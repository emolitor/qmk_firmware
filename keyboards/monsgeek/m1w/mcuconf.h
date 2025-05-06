// Copyright 2024 (@priyadie)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include_next <mcuconf.h>

#undef WB32_SPI_USE_QSPI
#define WB32_SPI_USE_QSPI TRUE

#undef WB32_SERIAL_USE_UART1
#define WB32_SERIAL_USE_UART1 TRUE
