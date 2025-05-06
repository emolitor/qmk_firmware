// Copyright 2024 sdk66 (@sdk66)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define USB_POWER_EN_PIN                    B1 // USB ENABLE pin
#define LED_POWER_EN_PIN                    A8 // LED ENABLE pin
#define LED_POWER_EN_2_PIN                  A9 // LED ENABLE pin
#define HS_BAT_CABLE_PIN                    A7 // USB insertion detection pin
#define HS_LED_BOOSTING_PIN                 B9 // LED BOOSTING

#define BAT_FULL_PIN                        A15
#define BAT_FULL_STATE                      1

#define HS_RGB_INDICATOR_COUNT              105
#define HS_RGB_BAT_COUNT                    1

#define MD_BT1_NAME                         "M2 V5 VIA BT1"
#define MD_BT2_NAME                         "M2 V5 VIA BT2"
#define MD_BT3_NAME                         "M2 V5 VIA BT3"
#define MD_DONGLE_PRODUCT                   "MonsGeek 2.4G KB"

/* Device Connection RGB Indicator Light Index And Color */
#define HS_RGB_BLINK_INDEX_BT1              56
#define HS_RGB_BLINK_INDEX_BT2              55
#define HS_RGB_BLINK_INDEX_BT3              54
#define HS_RGB_BLINK_INDEX_2G4              53
#define HS_RGB_BLINK_INDEX_USB              52

#define HS_LBACK_COLOR_BT1                  RGB_BLUE
#define HS_LBACK_COLOR_BT2                  RGB_BLUE
#define HS_LBACK_COLOR_BT3                  RGB_BLUE
#define HS_LBACK_COLOR_2G4                  RGB_RED
#define HS_LBACK_COLOR_USB                  RGB_WHITE

#define HS_PAIR_COLOR_BT1                   RGB_BLUE
#define HS_PAIR_COLOR_BT2                   RGB_BLUE
#define HS_PAIR_COLOR_BT3                   RGB_BLUE
#define HS_PAIR_COLOR_2G4                   RGB_RED
#define HS_PAIR_COLOR_USB                   RGB_WHITE

/* Battery */
#define BATTERY_CAPACITY_LOW                15
#define BATTERY_CAPACITY_STOP               0
#define RGB_MATRIX_BAT_INDEX_MAP            {25, 26, 27, 28, 29, 30, 31, 32, 33, 34}

/* Status Indicator Lamp */
#define HS_MATRIX_BLINK_INDEX_BAT           96
#define HS_RGB_INDEX_CAPS                   60
#define HS_RGB_INDEX_WIN_LOCK               94

#define HS_RGB_BLINK_INDEX_WIN              53
#define HS_RGB_BLINK_INDEX_MAC              54

#define SYSTEM_WIN_PIN                      C15
#define SYSTEM_MAC_PIN                      C14

/* UART */
#define SERIAL_DRIVER                       SD3
#define SD1_TX_PIN                          C10
#define SD1_RX_PIN                          C11

/* Encoder */
#define ENCODER_MAP_KEY_DELAY               1

/* SPI */
#define SPI_DRIVER                          SPIDQ
#define SPI_SCK_PIN                         B3
#define SPI_MOSI_PIN                        B5
#define SPI_MISO_PIN                        B4

/* Flash */
#define EXTERNAL_FLASH_SPI_SLAVE_SELECT_PIN C12
#define WEAR_LEVELING_LOGICAL_SIZE          (WEAR_LEVELING_BACKING_SIZE / 2)

/* RGB Light */
#define RGBLED_NUM 6
#define RGBLIGHT_LED_COUNT RGBLED_NUM
#define RGBLIGHT_CUSTOM_DRIVER
#define RGBLIGHT_DISABLE_KEYCODES
#define BREATHING_VAL           40
#define RAINBOW_MOOD            40
#define LED_TYPE                RGB
#define RGBLIGHT_LED_MAP {0, 1, 2, 3, 4, 5}

/* RGB Matrix */
#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#define RGB_MATRIX_KEYPRESSES

/* WS2812 */
#define WS2812_SPI_DRIVER  SPIDM2
#define WS2812_SPI_DIVISOR 32

/* rgb_record */
#define ENABLE_RGB_MATRIX_RGBR_PLAY
#define RGBREC_CHANNEL_NUM         4
#define EECONFIG_CONFINFO_USE_SIZE (4 + 16)
#define EECONFIG_RGBREC_USE_SIZE   (RGBREC_CHANNEL_NUM * MATRIX_ROWS * MATRIX_COLS * 2)
#define EECONFIG_USER_DATA_SIZE    (EECONFIG_RGBREC_USE_SIZE + EECONFIG_CONFINFO_USE_SIZE)
#define RGBREC_EECONFIG_ADDR       (uint8_t *)(EECONFIG_USER_DATABLOCK)
#define CONFINFO_EECONFIG_ADDR     (uint32_t *)((uint32_t)RGBREC_EECONFIG_ADDR + (uint32_t)EECONFIG_RGBREC_USE_SIZE)
