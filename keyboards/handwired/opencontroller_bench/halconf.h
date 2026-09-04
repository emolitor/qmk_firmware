// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Headroom for the trace hooks: the default 128 bytes overflow after 11 ms of
// continuous 115200 traffic.
#define SERIAL_BUFFERS_SIZE 256

#include_next <halconf.h>
