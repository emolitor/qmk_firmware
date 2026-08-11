// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#define HAL_USE_I2C TRUE

#ifdef OPENBOOT_BRIDGE_ENABLE
// The bridge forwards a 115200 baud stream through the main loop, so the input
// queue has to outlast the longest blocking iteration. The default 128 bytes
// overflow after 11ms of continuous traffic, which is uncomfortably close to
// the ~9ms an IS31FL3741 matrix flush can occupy on its own.
#    define SERIAL_BUFFERS_SIZE 256
#endif

#include_next <halconf.h>
