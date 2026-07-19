// Copyright 2022 Nick Brassel (@tzarc)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#ifndef __ASSEMBLER__
#    include <hal.h>
#endif

// The RP EFL LLD advertises the flash size it is configured for; reuse it so
// the wear-leveling area lands in the same place as the legacy rp2040_flash
// driver (top of flash).
#if defined(QMK_MCU_FAMILY_RP) && !defined(WEAR_LEVELING_EFL_FLASH_SIZE)
#    define WEAR_LEVELING_EFL_FLASH_SIZE RP_FLASH_SIZE
#endif

// Work out how many bytes per write to internal flash
#ifndef BACKING_STORE_WRITE_SIZE
// These need to match EFL's XXXXXX_FLASH_LINE_SIZE, see associated code in `lib/chibios/os/hal/ports/**/hal_efl_lld.c`,
// or associated `stm32_registry.h` for the MCU in question (or equivalent for the family).
#    if defined(QMK_MCU_SERIES_GD32VF103)
#        define BACKING_STORE_WRITE_SIZE 2 // from hal_efl_lld.c
#    elif defined(QMK_MCU_FAMILY_NUC123)
#        define BACKING_STORE_WRITE_SIZE 4 // from hal_efl_lld.c
#    elif defined(QMK_MCU_FAMILY_WB32)
#        define BACKING_STORE_WRITE_SIZE 8 // from hal_efl_lld.c
#    elif defined(QMK_MCU_FAMILY_AT32)
#        define BACKING_STORE_WRITE_SIZE 4 // from hal_efl_lld.c
#    elif defined(QMK_MCU_FAMILY_RP)
#        define BACKING_STORE_WRITE_SIZE 2 // RP EFL programs arbitrary offsets (page-buffered); 2 matches the legacy rp2040_flash format
#    elif defined(QMK_MCU_FAMILY_STM32)
#        if defined(STM32_FLASH_LINE_SIZE) // from some family's stm32_registry.h file
#            define BACKING_STORE_WRITE_SIZE (STM32_FLASH_LINE_SIZE)
#        else
#            if defined(QMK_MCU_SERIES_STM32F0XX)
#                define BACKING_STORE_WRITE_SIZE 2 // from hal_efl_lld.c
#            elif defined(QMK_MCU_SERIES_STM32F1XX)
#                define BACKING_STORE_WRITE_SIZE 2 // from hal_efl_lld.c
#            elif defined(QMK_MCU_SERIES_STM32F3XX)
#                define BACKING_STORE_WRITE_SIZE 2 // from hal_efl_lld.c
#            elif defined(QMK_MCU_SERIES_STM32F4XX)
#                define BACKING_STORE_WRITE_SIZE (1 << STM32_FLASH_PSIZE) // from hal_efl_lld.c
#            elif defined(QMK_MCU_SERIES_STM32L4XX)
#                define BACKING_STORE_WRITE_SIZE 8 // from hal_efl_lld.c
#            elif defined(QMK_MCU_SERIES_STM32G0XX)
#                define BACKING_STORE_WRITE_SIZE 8 // from hal_efl_lld.c
#            elif defined(QMK_MCU_SERIES_STM32G4XX)
#                define BACKING_STORE_WRITE_SIZE 8 // from hal_efl_lld.c
#            else
#                error "ChibiOS hasn't defined STM32_FLASH_LINE_SIZE, and could not automatically determine BACKING_STORE_WRITE_SIZE" // normally defined in stm32_registry.h, should be set by STM32_FLASH_LINE_SIZE
#            endif
#        endif
#    else
#        error "Could not automatically determine BACKING_STORE_WRITE_SIZE"
#    endif
#endif

#ifndef WEAR_LEVELING_BACKING_SIZE
#    if defined(QMK_MCU_FAMILY_RP)
// 8kB backing space (two flash sectors), matching the legacy rp2040_flash
// driver so existing EEPROM contents survive the backend switch
#        define WEAR_LEVELING_BACKING_SIZE 8192
#    else
// 2kB backing space allocated
#        define WEAR_LEVELING_BACKING_SIZE 2048
#    endif
#endif // WEAR_LEVELING_BACKING_SIZE

// 1kB logical EEPROM
#ifndef WEAR_LEVELING_LOGICAL_SIZE
#    define WEAR_LEVELING_LOGICAL_SIZE ((WEAR_LEVELING_BACKING_SIZE) / 2)
#endif // WEAR_LEVELING_LOGICAL_SIZE
