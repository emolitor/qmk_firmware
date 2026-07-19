// Copyright 2026 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef MCUCONF_H
#define MCUCONF_H

#define RP2350_MCUCONF

/*
 * HAL driver system settings.
 */
#define RP_NO_INIT                          FALSE
#define RP_CORE1_START                      FALSE

/*
 * IRQ system settings: the Cortex-M33 uses priorities 15...0 (lowest to
 * highest), the Hazard3 NVIC shim uses 3...0 -- the values below are valid
 * on both architectures.
 */
#define RP_IRQ_SYSTICK_PRIORITY             2
#define RP_IRQ_TIMER0_ALARM0_PRIORITY       2
#define RP_IRQ_TIMER0_ALARM1_PRIORITY       2
#define RP_IRQ_TIMER0_ALARM2_PRIORITY       2
#define RP_IRQ_TIMER0_ALARM3_PRIORITY       2
#define RP_IRQ_UART0_PRIORITY               3
#define RP_IRQ_UART1_PRIORITY               3
#define RP_IRQ_SPI0_PRIORITY                2
#define RP_IRQ_SPI1_PRIORITY                2
#define RP_IRQ_I2C0_PRIORITY                2
#define RP_IRQ_I2C1_PRIORITY                2
#define RP_IRQ_USB0_PRIORITY                3

/*
 * ADC driver system settings.
 */
#define RP_ADC_USE_ADC1                     FALSE

/*
 * SIO (UART) driver system settings.
 */
#define RP_SIO_USE_UART0                    FALSE
#define RP_SIO_USE_UART1                    FALSE

/*
 * SPI driver system settings.
 */
#define RP_SPI_USE_SPI0                     FALSE
#define RP_SPI_USE_SPI1                     FALSE
#define RP_SPI_SPI0_RX_DMA_CHANNEL          RP_DMA_CHANNEL_ID_ANY
#define RP_SPI_SPI0_TX_DMA_CHANNEL          RP_DMA_CHANNEL_ID_ANY
#define RP_SPI_SPI1_RX_DMA_CHANNEL          RP_DMA_CHANNEL_ID_ANY
#define RP_SPI_SPI1_TX_DMA_CHANNEL          RP_DMA_CHANNEL_ID_ANY
#define RP_SPI_SPI0_DMA_PRIORITY            1
#define RP_SPI_SPI1_DMA_PRIORITY            1
#define RP_SPI_DMA_ERROR_HOOK(spip)

/*
 * PWM driver system settings (the RP2350 has 12 slices).
 */
#define RP_PWM_USE_PWM0                     FALSE
#define RP_PWM_USE_PWM1                     FALSE
#define RP_PWM_USE_PWM2                     FALSE
#define RP_PWM_USE_PWM3                     FALSE
#define RP_PWM_USE_PWM4                     FALSE
#define RP_PWM_USE_PWM5                     FALSE
#define RP_PWM_USE_PWM6                     FALSE
#define RP_PWM_USE_PWM7                     FALSE
#define RP_PWM_USE_PWM8                     FALSE
#define RP_PWM_USE_PWM9                     FALSE
#define RP_PWM_USE_PWM10                    FALSE
#define RP_PWM_USE_PWM11                    FALSE

/*
 * I2C driver system settings.
 */
#define RP_I2C_USE_I2C0                     FALSE
#define RP_I2C_USE_I2C1                     FALSE

/*
 * USB driver system settings.
 */
#define RP_USB_USE_USB1                     TRUE
#define RP_USB_FORCE_VBUS_DETECT            TRUE
#define RP_USE_EXTERNAL_VBUS_DETECT         FALSE
#define RP_USB_USE_ERROR_DATA_SEQ_INTR      FALSE

#endif /* MCUCONF_H */
