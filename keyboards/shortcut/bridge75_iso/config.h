// Copyright 2025 emolitor (github.com/emolitor)
// Copyright 2024 Wind (@yelishang)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Necessary to set a long tapping term for the wireless configurations
#define TAPPING_TERM_PER_KEY
#define WIRELESS_TAPPING_TERM 3000

#define MD_BT1_NAME "Bridge75 BT1"
#define MD_BT2_NAME "Bridge75 BT2"
#define MD_BT3_NAME "Bridge75 BT3"
#define MD_DONGLE_PRODUCT "Bridge75 Dongle"

#ifdef WIRELESS_ENABLE
  #define LPWR_TIMEOUT RGB_MATRIX_TIMEOUT
  #define WLS_KEYBOARD_REPORT_KEYS 5
#endif

/* PINS */
#define USB_POWER_EN_PIN A14
#define LED_POWER_EN_PIN B7
#define BT_CABLE_PIN B8 // 充电接入时为高 High when charging
#define BT_CHARGE_PIN B9 // 充电时为低，充满时为高 Low when charging, high when fully charged

/* UART */
#define UART_TX_PIN A9
#define UART_TX_PAL_MODE 7
#define UART_RX_PIN A10
#define UART_RX_PAL_MODE 7

/* FLASH */
#define SPI_DRIVER SPIDQ
#define SPI_SCK_PIN B3
#define SPI_MOSI_PIN B5
#define SPI_MISO_PIN B4
#define SPI_MOSI_PAL_MODE 5
#define EXTERNAL_FLASH_SPI_SLAVE_SELECT_PIN C12
#define WEAR_LEVELING_LOGICAL_SIZE (WEAR_LEVELING_BACKING_SIZE / 2)

/* RGB */
#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#define RGB_MATRIX_KEYPRESSES
#define WS2812_TIMING 1250
#define WS2812_T1H 900
#define WS2812_T0H 350
#define WS2812_TRST_US 280
