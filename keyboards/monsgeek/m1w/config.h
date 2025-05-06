// Copyright 2024 (@priyadie)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef WIRELESS_ENABLE
#    define LPWR_TIMEOUT RGB_MATRIX_TIMEOUT
#    define MD_BT1_NAME "M1W VIA BT1"
#    define MD_BT2_NAME "M1W VIA BT2"
#    define MD_BT3_NAME "M1W VIA BT3"
#endif

#define USB_POWER_EN_PIN A5
#define LED_POWER_EN_PIN D2

#define RGB_MATRIX_BAT_INDEX_MAP {15, 16, 17, 18, 19, 20, 21, 22, 23, 24}

/* UART */
#define UART_TX_PIN A9
#define UART_RX_PIN A10

/* Encoder */
#define ENCODER_MAP_KEY_DELAY 1

/* SPI */
#define SPI_DRIVER SPIDQ
#define SPI_SCK_PIN B3
#define SPI_MOSI_PIN B5
#define SPI_MISO_PIN B4

/* Flash */
#define EXTERNAL_FLASH_SPI_SLAVE_SELECT_PIN C12
#define WEAR_LEVELING_LOGICAL_SIZE (WEAR_LEVELING_BACKING_SIZE / 2)

/* RGB Matrix */
#define DRIVER_1_CS C11
#define DRIVER_1_EN D2
#define DRIVER_2_CS C10
#define DRIVER_2_EN D2
#define DRIVER_COUNT 2
