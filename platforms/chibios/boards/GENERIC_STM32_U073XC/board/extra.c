// Copyright 2025 Stefan Kerkmann (@karlk90)
// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <hal.h>

#define FLASH_KEY1 0x45670123U
#define FLASH_KEY2 0xCDEF89ABU
#define FLASH_OPTKEY1 0x08192A3BU
#define FLASH_OPTKEY2 0x4C5D6E7FU
#define FLASH_OPTR_CLR_MASK (FLASH_OPTR_nBOOT_SEL)
#define FLASH_OPTR_SET_MASK (FLASH_OPTR_NRST_MODE_Msk)

static void wait_for_flash(void) {
    while (READ_BIT(FLASH->SR, FLASH_SR_BSY1)) {
    }
}

void __attribute__((constructor)) enable_boot0_and_nrst_pin(void) {
    // Only apply on STM32U0 devices, see RM0503 Rev 3, Section 26.4.1:
    // "DBGMCU device ID code register". The DBGMCU peripheral is clock
    // gated on the U0 and must be enabled before IDCODE can be read.
    SET_BIT(RCC->DBGCFGR, RCC_DBGCFGR_DBGEN);
    switch (READ_BIT(DBGMCU->IDCODE, DBGMCU_IDCODE_DEV_ID)) {
        case 0x489: // STM32U031xx, STM32U073xx and STM32U083xx
            break;
        default:
            return;
    }

    uint32_t optr = FLASH->OPTR;

    // Make sure that:
    // 1. legacy boot0 pin handling is enabled.
    //   OPTR[24] = 0
    // 2. legacy nRST pin handling is enabled.
    //   OPTR[28:27] = 0b11
    // To match the default behavior found in older (F0/F1/F3/F4) STM32 devices.
    if (READ_BIT(optr, FLASH_OPTR_CLR_MASK) || (READ_BIT(optr, FLASH_OPTR_SET_MASK) != FLASH_OPTR_SET_MASK)) {
        if (READ_BIT(FLASH->CR, FLASH_CR_LOCK)) {
            WRITE_REG(FLASH->KEYR, FLASH_KEY1);
            WRITE_REG(FLASH->KEYR, FLASH_KEY2);
            while (READ_BIT(FLASH->CR, FLASH_CR_LOCK)) {
            }
            wait_for_flash();
        }
        if (READ_BIT(FLASH->CR, FLASH_CR_OPTLOCK)) {
            WRITE_REG(FLASH->OPTKEYR, FLASH_OPTKEY1);
            WRITE_REG(FLASH->OPTKEYR, FLASH_OPTKEY2);
            while (READ_BIT(FLASH->CR, FLASH_CR_OPTLOCK)) {
            }
            wait_for_flash();
        }

        MODIFY_REG(FLASH->OPTR, FLASH_OPTR_CLR_MASK, FLASH_OPTR_SET_MASK);
        wait_for_flash();

        SET_BIT(FLASH->CR, FLASH_CR_OPTSTRT);
        wait_for_flash();

        CLEAR_BIT(FLASH->CR, FLASH_CR_OPTSTRT);
        wait_for_flash();

        // Launch the option byte (re)loading, which resets the device. This
        // should not return.
        SET_BIT(FLASH->CR, FLASH_CR_OBL_LAUNCH);
    }
}

void __attribute__((constructor)) enable_crs_autotrim(void) {
    // Crystal-less USB: the raw HSI48 is far outside the USB full-speed
    // tolerance, so let the CRS continuously trim it against the 1 kHz USB
    // SOF. The CRS reset defaults already select USB SOF as the sync source
    // with a 48 MHz target, only enabling is required. Constructors run
    // after __early_init()/stm32_clock_init(), so HSI48 is already up.
    rccEnableAPBR1(RCC_APBENR1_CRSEN, false);
    SET_BIT(CRS->CR, CRS_CR_AUTOTRIMEN | CRS_CR_CEN);
}
