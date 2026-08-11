// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Send one report on the OpenBoot bridge IN endpoint.
 *
 * The buffer must hold exactly OPENBOOT_EPSIZE bytes. Returns false while a
 * previous report is still in flight; the caller is expected to offer the same
 * bytes again rather than to queue a second one.
 */
bool openboot_usb_send(const uint8_t *report);

/**
 * @brief Take one report from the OpenBoot bridge OUT endpoint, if any.
 *
 * The buffer must have room for OPENBOOT_EPSIZE bytes. Never blocks.
 */
bool openboot_usb_receive(uint8_t *report);
