// Copyright 2025 emolitor (github.com/emolitor)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/*
 * Specific tapping term for wireless configuration. If the key is held for
 * less than 3 seconds we select that device for the active connection. If
 * greater than 3 seconds we select that device and go into pairing mode.
 */
#define WIRELESS_TAPPING_TERM 3000
/*
 * After a connection, light the LED under the number key associated with the
 * device for 5 seconds, then turn the LED off to save power.
 */
#define WIRELESS_LED_CONNECT_CONFIRM_TIME 5000

/* LEDS */
#define LED_BLINK_FAST_PERIOD_MS 300
#define RGB_ADJ_WHITE   0xC8, 0xC8, 0xC8

/* KEYS */
#define ESCAPE_PIN      D2
#define DEVS_BT1_PIN    C11
#define DEVS_BT2_PIN    C10
#define DEVS_BT3_PIN    A15
#define DEVS_2G4_PIN    C0

/* FLASH */
#define SPI_DRIVER SPIDQ
#define SPI_SCK_PIN B3
#define SPI_MOSI_PIN B5
#define SPI_MISO_PIN B4
#define SPI_MOSI_PAL_MODE 5
#define EXTERNAL_FLASH_SPI_SLAVE_SELECT_PIN C12

/* POWER */
#define USB_POWER_EN_PIN A14
#define LED_POWER_EN_PIN B13
#define BT_CABLE_PIN B8 // High when charging
#define BT_CHARGE_PIN B9 // Low when charging, high when fully charged

/* UART */
#define UART_TX_PIN A9
#define UART_TX_PAL_MODE 7
#define UART_RX_PIN A10
#define UART_RX_PAL_MODE 7

/* WIRELESS NAMES */
#define MD_BT_NAME "NEO80 BT$"

#define LPWR_TIMEOUT 120000
#define WLS_KEYBOARD_REPORT_KEYS 5
