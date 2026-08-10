// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// IS31FL3741A on I2C1 (SCL B6 / SDA B7), ADDR strapped to GND, SDB on B5
// with an external pulldown.
#define IS31FL3741_I2C_ADDRESS_1 IS31FL3741_I2C_ADDRESS_GND
#define IS31FL3741_SDB_PIN B5

// I2Cv4 timing for a 56 MHz I2C kernel clock (PCLK), Fast-mode 400 kHz:
// t_low 1428 ns, t_high 821 ns.
#define I2C1_TIMINGR_PRESC 1
#define I2C1_TIMINGR_SCLDEL 3
#define I2C1_TIMINGR_SDADEL 2
#define I2C1_TIMINGR_SCLH 22
#define I2C1_TIMINGR_SCLL 39

#ifdef OPENCONTROLLER_ENABLE
// B01 does not cross the inter-MCU UART. USART2 SWAP makes PA2 receive from
// the CH592 TX and PA3 transmit to the CH592 RX.
#    define UART_DRIVER SD2
#    define UART_TX_PIN A2
#    define UART_RX_PIN A3
#    define UART_TX_PAL_MODE 7
#    define UART_RX_PAL_MODE 7
#    define UART_CR2 USART_CR2_SWAP
#endif
