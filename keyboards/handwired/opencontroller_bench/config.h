// Copyright 2026 Eric Molitor (@emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// NUCLEO-U083RC to a CH592F running the opencontroller-ch592 profile
// (UART1 remapped to PB12/PB13).
// Default: USART1 on pins the ST-Link never touches (see readme.md).
//   PA9  USART1_TX (AF7), CN10 pin 21 -> CH592F PB12 RXD1 (the module's wake edge)
//   PA10 USART1_RX (AF7), CN10 pin 33 <- CH592F PB13 TXD1
// BENCH_UART=2 selects USART2 on PA2/PA3 instead. Those are the ST-Link VCP
// lines: the host then sees a copy of the STM32's TX, but the ST-Link's own TX
// driver holds PA3 high and the module's replies never arrive unless the VCP
// TX solder bridge has been removed.
#ifdef OPENCONTROLLER_BENCH_VCP_UART
#    define UART_DRIVER SD2
#    define UART_TX_PIN A2
#    define UART_RX_PIN A3
#else
#    define UART_DRIVER SD1
#    define UART_TX_PIN A9
#    define UART_RX_PIN A10
#endif
#define UART_TX_PAL_MODE 7
#define UART_RX_PAL_MODE 7

// Bench value: exercise the idle sleep path in seconds rather than minutes.
#ifndef OPENCONTROLLER_SLEEP_TIMEOUT_MS
#    define OPENCONTROLLER_SLEEP_TIMEOUT_MS 20000
#endif
