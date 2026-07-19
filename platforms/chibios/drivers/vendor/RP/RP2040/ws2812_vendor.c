// Copyright 2022 Stefan Kerkmann (@KarlK90)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ws2812.h"

#include <hal.h>

#include "gpio.h"
#include "debug.h"
#include "wait.h"
#include "util.h"

#if !defined(MCU_RP)
#    error PIO Driver is only available for Raspberry Pi RP MCUs!
#endif

#if defined(WS2812_PIO_USE_PIO1)
static const rp_pio_block_t *pio_block = RP_PIO1_BLOCK;
#else
static const rp_pio_block_t *pio_block = RP_PIO0_BLOCK;
#endif

#if !defined(RP_DMA_PRIORITY_WS2812)
#    define RP_DMA_PRIORITY_WS2812 3
#endif

#if defined(WS2812_EXTERNAL_PULLUP)
#    pragma message "The GPIOs of the RP family are NOT 5V tolerant! Make sure to NOT apply any voltage over 3.3V to the RGB data pin."
#endif

/*================== WS2812 PIO TIMINGS =================*/

// WS2812_T1L rounded to 50ns intervals and split into two wait timings
#define PIO_T1L (WS2812_T1L / 50)
#define PIO_T1L_A (MAX(CEILING(PIO_T1L, 2) - 1, 0))
#define PIO_T1L_B (MAX(PIO_T1L / 2 - 1, 0))

// WS2812_T0L rounded to 50ns intervals
#define PIO_T0L (MAX(WS2812_T0L / 50 - PIO_T1L, 0))
#define PIO_T0L_A (MAX(PIO_T0L - 1, 0))

// WS2812_T0H rounded to 50ns intervals
#define PIO_T0H (WS2812_T0H / 50)
#define PIO_T0H_A MAX(PIO_T0H - 1, 0)

// WS2812_T1H rounded to 50ns intervals and split into two wait timings
#define PIO_T1H (MAX(WS2812_T1H / 50 - PIO_T0H, 0))
#define PIO_T1H_A (MAX((CEILING(PIO_T1H, 2) - 1), 0))
#define PIO_T1H_B (MAX((PIO_T1H / 2) - 1, 0))

#if (WS2812_T0L % 50) != 0
#    pragma message "WS2812_T0L is not given in an 50ns interval, it will be rounded to the next 50ns"
#endif

#if (WS2812_T0H % 50) != 0
#    pragma message "WS2812_T0H is not given in an 50ns interval, it will be rounded to the next 50ns"
#endif

#if (WS2812_T1L % 50) != 0
#    pragma message "WS2812_T0L is not given in an 50ns interval, it will be rounded to the next 50ns"
#endif

#if (WS2812_T1H % 50) != 0
#    pragma message "WS2812_T0H is not given in an 50ns interval, it will be rounded to the next 50ns"
#endif

#if WS2812_T0L < WS2812_T1L
#    error WS2812_T0L is shorter than WS2812_T1L, this is impossible to express in the RP PIO driver. Please correct your timings.
#endif

#if WS2812_T1H < WS2812_T0H
#    error WS2812_T1H is shorter than WS2812_T0H, this is impossible to express in the RP PIO driver. Please correct your timings.
#endif

#if WS2812_T0L > (850 + WS2812_T1L)
#    error WS2812_T0L is longer than 850ns + WS2812_T1L, this is impossible to express in the RP PIO driver. Please correct your timings.
#endif

#if WS2812_T0H > 850
#    error WS2812_T0H is longer than 850ns, this is impossible to express in the RP PIO driver. Please correct your timings.
#endif

#if WS2812_T1H > (1700 + WS2812_T0H)
#    error WS2812_T1H is longer than 1700ns + WS2812_T0H, this is impossible to express in the RP PIO driver. Please correct your timings.
#endif

#if WS2812_T1L > 1700
#    error WS2812_T1L is longer than 1700ns, this is impossible to express in the RP PIO driver. Please correct your timings.
#endif

#if WS2812_T0L < (50 + WS2812_T1L)
#    error WS2812_T0L is shorter than 50ns + WS2812_T1L, this is impossible to express in the RP PIO driver. Please correct your timings.
#endif

#if WS2812_T0H < 50
#    error WS2812_T0H is shorter than 50ns, this is impossible to express in the RP PIO driver. Please correct your timings.
#endif

#if WS2812_T1H < (100 + WS2812_T0H)
#    error WS2812_T1H is longer than 100ns + WS2812_T0H, this is impossible to express in the RP PIO driver. Please correct your timings.
#endif

#if WS2812_T1L < 100
#    error WS2812_T1L is longer than 1700ns, this is impossible to express in the RP PIO driver. Please correct your timings.
#endif

/**
 * @brief Helper macro to binary patch the delay part of an per-compiled PIO
 * opcode.
 */
#define PIO_DELAY(delay, opcode) (((delay & 0xF) << 8U) | opcode)

#define WS2812_WRAP_TARGET 0
#define WS2812_WRAP 5

static const uint16_t ws2812_program_instructions[] = {
    //     .wrap_target
    PIO_DELAY(PIO_T1L_A, 0x6021), //  0: out    x, 1            side 0  // T1L (max. 1700ns)
    PIO_DELAY(PIO_T1L_B, 0xa042), //  1: nop                    side 0  // T1L
    PIO_DELAY(PIO_T0H_A, 0x1025), //  2: jmp    !x, 5           side 1  // T0H (max. 850ns)
    PIO_DELAY(PIO_T1H_A, 0xb042), //  3: nop                    side 1  // T1H (max. 1700ns + T0H)
    PIO_DELAY(PIO_T1H_B, 0x1000), //  4: jmp    0               side 1  // T1H
    PIO_DELAY(PIO_T0L_A, 0xa042), //  5: nop                    side 0  // T0L (max. 850ns + T1L)
    //     .wrap
};

static const rp_pio_program_t ws2812_program = {
    .instructions = ws2812_program_instructions,
    .length       = ARRAY_SIZE(ws2812_program_instructions),
    .origin       = -1,
};

static uint32_t                WS2812_BUFFER[WS2812_LED_COUNT];
static const rp_dma_channel_t *dma_channel;
static uint32_t                RP_DMA_MODE_WS2812;
static const rp_pio_sm_t      *state_machine = NULL;

static SEMAPHORE_DECL(TRANSFER_COUNTER, 1);
static volatile systime_t LAST_TRANSFER_DEADLINE;

// Upper bound for the remaining transfer time: a full 8 entry TX FIFO plus
// the OSR, each holding a 32-bit frame with worst case bit timings, plus the
// latch time. Used to detect an already elapsed deadline after wrap-around.
#define WS2812_MAX_WAIT_US (9U * 32U * 4U + WS2812_TRST_US)

/**
 * @brief Convert RGBW value into WS2812 compatible 32-bit data word.
 */
__always_inline static uint32_t rgbw8888_to_u32(uint8_t red, uint8_t green, uint8_t blue, uint8_t white) {
#if (WS2812_BYTE_ORDER == WS2812_BYTE_ORDER_GRB)
    return ((uint32_t)green << 24) | ((uint32_t)red << 16) | ((uint32_t)blue << 8) | ((uint32_t)white);
#elif (WS2812_BYTE_ORDER == WS2812_BYTE_ORDER_RGB)
    return ((uint32_t)red << 24) | ((uint32_t)green << 16) | ((uint32_t)blue << 8) | ((uint32_t)white);
#elif (WS2812_BYTE_ORDER == WS2812_BYTE_ORDER_BGR)
    return ((uint32_t)blue << 24) | ((uint32_t)green << 16) | ((uint32_t)red << 8) | ((uint32_t)white);
#endif
}

static inline uint32_t pio_tx_fifo_level(void) {
    return (pio_block->pio->FLEVEL >> (8U * state_machine->smidx)) & 0xFU;
}

static void ws2812_dma_callback(void *p, uint32_t ct) {
    // We assume that there is at least one frame left in the OSR even if the TX
    // FIFO is already empty.
    uint32_t time_to_completion = (pio_tx_fifo_level() + 1U) * MAX(WS2812_T1H + WS2812_T1L, WS2812_T0H + WS2812_T0L);

#if defined(WS2812_RGBW)
    time_to_completion *= 32;
#else
    time_to_completion *= 24;
#endif

    // Convert from ns to us
    time_to_completion /= 1000;

    LAST_TRANSFER_DEADLINE = chVTGetSystemTimeX() + TIME_US2I(time_to_completion + WS2812_TRST_US);

    osalSysLockFromISR();
    chSemSignalI(&TRANSFER_COUNTER);
    osalSysUnlockFromISR();
}

void ws2812_init(void) {
    // clang-format off
    iomode_t rgb_pin_mode = PAL_RP_PAD_SLEWFAST |
                            PAL_RP_GPIO_OE |
#if defined(WS2812_EXTERNAL_PULLUP)
                            PAL_RP_IOCTRL_OEOVER_DRVINVPERI |
#endif
                            (pio_block->pioidx == 0 ? PAL_MODE_ALTERNATE_PIO0 : PAL_MODE_ALTERNATE_PIO1);
    // clang-format on

    palSetLineMode(WS2812_DI_PIN, rgb_pin_mode);

    // The allocation also releases the PIO block from reset and configures its
    // interrupt vector; no PIO interrupt sources are used by this driver.
    state_machine = pioSmAlloc(pio_block, RP_PIO_SM_ID_ANY, CORTEX_MAX_KERNEL_PRIORITY, NULL, NULL);
    if (state_machine == NULL) {
        dprintln("ERROR: Failed to acquire state machine for WS2812 output!");
        return;
    }

    int32_t offset = pioProgramLoad(pio_block, &ws2812_program);
    if (offset < 0) {
        dprintln("ERROR: Failed to load WS2812 PIO program!");
        pioSmFree(state_machine);
        state_machine = NULL;
        return;
    }

    // Set the data pin direction to output via a 'set pindirs, 1' instruction.
    pioSmSetPinctrlX(state_machine, (1U << PIO_SM_PINCTRL_SET_COUNT_Pos) | ((uint32_t)WS2812_DI_PIN << PIO_SM_PINCTRL_SET_BASE_Pos));
    pioSmExecX(state_machine, 0xE081U);

    // One side-set bit on the data pin, no SET/OUT pin usage by the program.
    pioSmSetPinctrlX(state_machine, (1U << PIO_SM_PINCTRL_SIDESET_COUNT_Pos) | ((uint32_t)WS2812_DI_PIN << PIO_SM_PINCTRL_SIDESET_BASE_Pos));

    // clang-format off
    uint32_t execctrl = PIO_SM_EXECCTRL_WRAP(offset + WS2812_WRAP_TARGET,
                                             offset + WS2812_WRAP);
#if defined(WS2812_EXTERNAL_PULLUP)
    /* Instruct side-set to change the pin-directions instead of outputting
     * a logic level. We generate our levels the following way:
     *
     * 1: Set RGB data pin to high impedance input and let the pull-up drive the
     * signal high.
     *
     * 0: Set RGB data pin to low impedance output and drive the pin low.
     */
    execctrl |= PIO_SM_EXECCTRL_SIDE_PINDIR;
#endif
    pioSmSetExecctrlX(state_machine, execctrl);

    // Shift out left (MSB first) with autopull, TX FIFO only.
#if defined(WS2812_RGBW)
    pioSmSetShiftctrlX(state_machine, PIO_SM_SHIFTCTRL_AUTOPULL |
                                      PIO_SM_SHIFTCTRL_FJOIN_TX |
                                      ((32U & 0x1FU) << PIO_SM_SHIFTCTRL_PULL_THRESH_Pos));
#else
    pioSmSetShiftctrlX(state_machine, PIO_SM_SHIFTCTRL_AUTOPULL |
                                      PIO_SM_SHIFTCTRL_FJOIN_TX |
                                      (24U << PIO_SM_SHIFTCTRL_PULL_THRESH_Pos));
#endif
    // clang-format on

    // Every instruction takes 50ns to execute with a clock speed of 20 MHz,
    // giving the WS2812 PIO driver its time resolution
    pioSmSetFrequencyX(state_machine, 20000000U);

    pioSmClearFifosX(state_machine);
    pioSmRestartX(state_machine);
    pioSmClkdivRestartX(state_machine);
    pioSmSetPCX(state_machine, (uint32_t)offset);
    pioSmEnableX(state_machine);

    dma_channel = dmaChannelAlloc(RP_DMA_CHANNEL_ID_ANY, RP_DMA_PRIORITY_WS2812, (rp_dmaisr_t)ws2812_dma_callback, NULL);
    dmaChannelEnableInterruptX(dma_channel);
    dmaChannelSetDestinationX(dma_channel, (uint32_t)&pio_block->pio->TXF[state_machine->smidx]);

    // clang-format off
    RP_DMA_MODE_WS2812 = DMA_CTRL_TRIG_INCR_READ |
                         DMA_CTRL_TRIG_DATA_SIZE_WORD |
                         DMA_CTRL_TRIG_TREQ_SEL(pio_block->pioidx * 8U + state_machine->smidx) |
                         (RP_DMA_PRIORITY_WS2812 > 0 ? DMA_CTRL_TRIG_HIGH_PRIORITY : 0U);
    // clang-format on
}

static inline void sync_ws2812_transfer(void) {
    if (chSemWaitTimeout(&TRANSFER_COUNTER, TIME_MS2I(WS2812_LED_COUNT)) == MSG_TIMEOUT) {
        // Abort the synchronization if we have to wait longer than the total
        // count of LEDs in milliseconds. This is safely much longer than it
        // would take to push all the data out.
        dprintln("ERROR: WS2812 DMA transfer has stalled, aborting!");
        dmaChannelDisableX(dma_channel);
        pioSmClearFifosX(state_machine);
        pioSmRestartX(state_machine);
        chSemReset(&TRANSFER_COUNTER, 0);
        wait_us(WS2812_TRST_US);
        return;
    }

    // Busy wait until the last transfer and the latch time have passed. If the
    // deadline is already behind us the unsigned difference wraps far above
    // the bound and no wait is performed.
    sysinterval_t remaining = chTimeDiffX(chVTGetSystemTime(), LAST_TRANSFER_DEADLINE);
    if (remaining <= (sysinterval_t)TIME_US2I(WS2812_MAX_WAIT_US)) {
        wait_us((uint16_t)TIME_I2US(remaining));
    }
}

ws2812_led_t ws2812_leds[WS2812_LED_COUNT];

void ws2812_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    ws2812_leds[index].r = red;
    ws2812_leds[index].g = green;
    ws2812_leds[index].b = blue;
#if defined(WS2812_RGBW)
    ws2812_rgb_to_rgbw(&ws2812_leds[index]);
#endif
}

void ws2812_set_color_all(uint8_t red, uint8_t green, uint8_t blue) {
    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        ws2812_set_color(i, red, green, blue);
    }
}

void ws2812_flush(void) {
    sync_ws2812_transfer();

    for (int i = 0; i < WS2812_LED_COUNT; i++) {
#if defined(WS2812_RGBW)
        WS2812_BUFFER[i] = rgbw8888_to_u32(ws2812_leds[i].r, ws2812_leds[i].g, ws2812_leds[i].b, ws2812_leds[i].w);
#else
        WS2812_BUFFER[i] = rgbw8888_to_u32(ws2812_leds[i].r, ws2812_leds[i].g, ws2812_leds[i].b, 0);
#endif
    }

    dmaChannelSetSourceX(dma_channel, (uint32_t)WS2812_BUFFER);
    dmaChannelSetCounterX(dma_channel, WS2812_LED_COUNT);
    dmaChannelSetModeX(dma_channel, RP_DMA_MODE_WS2812);
    dmaChannelEnableX(dma_channel);
}
