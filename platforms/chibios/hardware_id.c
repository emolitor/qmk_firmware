// Copyright 2022 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#include <ch.h>
#include <hal.h>
#include "hardware_id.h"

__attribute__((weak)) hardware_id_t get_hardware_id(void) {
    hardware_id_t id = {0};
#if defined(MCU_RP) && (HAL_USE_EFL == TRUE)
    // The flash JEDEC unique id doubles as the hardware id; reading it
    // requires the EFL driver (started idempotently here).
    eflStart(&EFLD1, NULL);
    efl_lld_read_unique_id(&EFLD1, (uint8_t *)&id);
#elif defined(UID_BASE)
    id.data[0] = (uint32_t)(*((uint32_t *)UID_BASE));
    id.data[1] = (uint32_t)(*((uint32_t *)(UID_BASE + 4)));
    id.data[2] = (uint32_t)(*((uint32_t *)(UID_BASE + 8)));
#endif
    return id;
}
