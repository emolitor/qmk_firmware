// Copyright 2024 (@priyadie)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define HAL_USE_SPI TRUE
#define SPI_USE_WAIT TRUE
#define HAL_USE_SERIAL TRUE
#define PAL_USE_CALLBACKS TRUE
#define SPI_SELECT_MODE SPI_SELECT_MODE_PAD

#include_next <halconf.h>
