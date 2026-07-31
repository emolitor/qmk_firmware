// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// The U0 flash raises a non-maskable interrupt on ECC double errors, and the
// wear-leveling ECC recovery in interrupt_handlers.c must own the NMI vector
// to absorb them. The default ARMv6-M port uses the NMI for its preemption
// trampoline; the alternate switch moves that to PendSV, freeing the NMI.
#define CORTEX_ALTERNATE_SWITCH TRUE

#include_next <chconf.h>
