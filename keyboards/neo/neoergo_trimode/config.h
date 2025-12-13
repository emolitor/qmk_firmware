// Copyright 2025 emolitor (github.com/emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/*
 * Specific tapping term for wireless configuration. If the key is held for
 * less than 3 seconds we select that device for the active connection. If
 * greater than 3 seconds we select that device and go into pairing mode.
 */
#define WIRELESS_TAPPING_TERM 3000

/* LEDS */
#define RGB_ADJ_WHITE   0xC8, 0xC8, 0xC8
#define DEVS_USB_INDEX  0
#define DEVS_BT1_INDEX  1
#define DEVS_BT2_INDEX  2
#define DEVS_BT3_INDEX  3
#define DEVS_2G4_INDEX  4

/* FLASH */
#define SPI_DRIVER SPIDQ
#define SPI_SCK_PIN B3
#define SPI_MOSI_PIN B5
#define SPI_MISO_PIN B4
#define SPI_MOSI_PAL_MODE 5
#define EXTERNAL_FLASH_SPI_SLAVE_SELECT_PIN C12

/* POWER */
#define USB_POWER_EN_PIN A14
#define LED_POWER_EN_PIN B7
#define BT_CABLE_PIN B8 // High when charging
#define BT_CHARGE_PIN B9 // Low when charging, high when fully charged

/* UART */
#define UART_TX_PIN A9
#define UART_TX_PAL_MODE 7
#define UART_RX_PIN A10
#define UART_RX_PAL_MODE 7

/* WIRELESS NAMES */
#define MD_BT_NAME "NEOERGO BT$"

#define LPWR_TIMEOUT 120000
#define WLS_KEYBOARD_REPORT_KEYS 5
