/*
 * WS2812 GPIO DMA Driver for WB32FQ95xx (Bridge75 Custom Implementation)
 *
 * Non-circular sequential block DMA with double-buffer architecture.
 * Timer-triggered DMA writes GPIO BSRR values using 3-phase bit encoding.
 *
 * Why GPIO+DMA works where PWM+DMA fails:
 * - PWM+DMA: Timer keeps running during ISR chunk transitions, generating
 *   extra PWM cycles with stale CCR values (data corruption)
 * - GPIO+DMA: GPIO pin holds its last state during ISR pause. Since we
 *   always end each bit LOW, the pause is just an extended low period
 *   which is within WS2812 spec (< 9µs reset threshold)
 *
 * Architecture:
 * - Quad buffer: 4 × 504 phases, ISR chains next block on TFR interrupt
 * - Fill thread pre-fills buf[0..3], then yields while ISR advances
 * - Block size 504: ≤511 hardware max, divisible by 3 (bit alignment)
 * - Reset: 300µs sleep after final DMA block (pin already LOW)
 *
 * Timing (3 phases per bit at 72MHz, PSC=0, ARR=29):
 *   Phase 1 (0-417ns):    Always SET (go high)
 *   Phase 2 (417-833ns):  Bit 0: RESET (go low), Bit 1: NOP (stay high)
 *   Phase 3 (833-1250ns): Bit 0: NOP (stay low), Bit 1: RESET (go low)
 *
 * Copyright 2026 emolitor (github.com/emolitor)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ws2812.h"
#include "gpio.h"
#include "timer.h"
#include "chibios_config.h"

#if defined(WB32F3G71xx) || defined(WB32FQ95xx)

#include "hal.h"

/* Validate required configuration */
#ifndef WS2812_DI_PIN
#    error "WS2812_GPIO_DMA driver requires WS2812_DI_PIN to be defined"
#endif

/* Guard against exceeding quad-buffer capacity.
 * 168 LEDs × 72 phases = 12096 = 24 blocks of 504. Beyond this,
 * the fill-time margin (630µs - 282µs = 348µs) may be insufficient. */
#if WS2812_LED_COUNT > 168
#    error "WS2812_LED_COUNT exceeds quad-buffer capacity (max 168)"
#endif

/* GPIO BSRR configuration - extract port and pin from QMK PAL line definition */
#ifndef WS2812_GPIO_PORT
#    define WS2812_GPIO_PORT PAL_PORT(WS2812_DI_PIN)
#endif

#ifndef WS2812_GPIO_PIN_NUM
#    define WS2812_GPIO_PIN_NUM PAL_PAD(WS2812_DI_PIN)
#endif

/* BSRR values for SET (bits 0-15) and RESET (bits 16-31) */
#define BSRR_SET   (1U << WS2812_GPIO_PIN_NUM)
#define BSRR_RESET (1U << (WS2812_GPIO_PIN_NUM + 16))
#define BSRR_NOP   0x00000000U

/* Timer configuration */
#ifndef WS2812_GPIO_DMA_TIMER
#    define WS2812_GPIO_DMA_TIMER GPTD3
#endif

#ifndef WS2812_GPIO_DMA_TIM_HWHIF
#    define WS2812_GPIO_DMA_TIM_HWHIF WB32_DMAC_HWHIF_TIM3_UP
#endif

/* DMA configuration */
#ifndef WS2812_GPIO_DMA_STREAM
#    define WS2812_GPIO_DMA_STREAM WB32_DMA1_STREAM1
#endif

/* Timing constants at 72MHz */
#define WS2812_TIMER_FREQ    72000000U
#define WS2812_PHASE_TICKS   30U        /* 30 ticks × 13.89ns = 417ns at 72MHz */

/* Color channel configuration */
#ifdef WS2812_RGBW
#    define WS2812_CHANNELS 4
#else
#    define WS2812_CHANNELS 3
#endif

/* Buffer size calculations */
#define WS2812_BITS_PER_LED    (WS2812_CHANNELS * 8)
#define WS2812_PHASES_PER_BIT  3
#define WS2812_PHASES_PER_LED  (WS2812_BITS_PER_LED * WS2812_PHASES_PER_BIT)  /* 72 phases per LED */

/* Block DMA configuration:
 * - Block size 504: ≤511 hardware max (9-bit BLOCK_TS), divisible by 3
 * - Quad buffer: 4 × 504 phases = 8064 bytes
 * - Total LED phases: WS2812_LED_COUNT × 72
 * - Block count: ceil(LED_PHASES / 504)
 */
#define WS2812_BLOCK_SIZE    504U
#define WS2812_LED_PHASES    (WS2812_LED_COUNT * WS2812_PHASES_PER_LED)
#define WS2812_BLOCK_COUNT   ((WS2812_LED_PHASES + WS2812_BLOCK_SIZE - 1) / WS2812_BLOCK_SIZE)

/* Reset pulse: 300µs sleep in thread context after final DMA block */
#define WS2812_RESET_US      300U

/* Safety timeout: abort DMA if transfer takes longer than this.
 * Normal 82-LED frame takes ~3ms. Must be shorter than the wireless
 * ACK timeout (MD_SNED_PKT_TIMEOUT = 10ms) to avoid triggering
 * wireless retry/drop logic during DMA stall recovery. */
#define WS2812_TIMEOUT_MS    5U

/* Quad buffer for block DMA — 4 buffers give 3-block-period (630µs) runway
 * which exceeds fill time (282µs), eliminating CPU/DMA data race */
static uint32_t ws2812_buf[4][WS2812_BLOCK_SIZE];

/* LED color storage for ws2812_set_color API */
ws2812_led_t ws2812_leds[WS2812_LED_COUNT];

/* Block DMA state */
static volatile uint32_t ws2812_block_idx;          /* Current block being DMA'd */
static volatile bool     ws2812_transfer_active;
static const wb32_dma_stream_t *ws2812_dma_stream;

#ifdef WS2812_DEBUG
static volatile uint32_t ws2812_dma_error_count;    /* DMA error counter for debugging */
#endif

/* GPT timer driver instance */
static GPTDriver *ws2812_gpt = &WS2812_GPIO_DMA_TIMER;

/* Forward declarations */
static void ws2812_dma_callback(void *p, uint32_t flags);
static void ws2812_fill_block(uint32_t *buf, uint32_t start_phase, uint32_t count);
static void ws2812_abort_transfer(void);

/* Timer configuration - no callback, DMA handles transfers */
static const GPTConfig ws2812_gpt_config = {
    .frequency = WS2812_TIMER_FREQ,
    .callback  = NULL,
    .cr2       = 0,
    .dier      = WB32_TIM_DIER_UDE,  /* Enable DMA request on update */
};

/**
 * @brief Get the number of phases in a given block
 */
static inline uint32_t ws2812_get_block_size(uint32_t block) {
    uint32_t remaining = WS2812_LED_PHASES - (block * WS2812_BLOCK_SIZE);
    return (remaining > WS2812_BLOCK_SIZE) ? WS2812_BLOCK_SIZE : remaining;
}

/**
 * @brief Fill a buffer with encoded LED phase data (optimized incremental)
 *
 * Uses division only once at block start, then increments through phases.
 * Accesses LED data as raw bytes in memory order, which automatically
 * respects WS2812_BYTE_ORDER (the ws2812_led_t struct layout changes
 * with the byte order setting).
 *
 * @param buf Pointer to buffer to fill (WS2812_BLOCK_SIZE elements)
 * @param start_phase Global phase index to start from
 * @param count Number of phases to fill
 */
static void ws2812_fill_block(uint32_t *buf, uint32_t start_phase, uint32_t count) {
    /* Compute initial position via division (once per block) */
    uint32_t led_idx      = start_phase / WS2812_PHASES_PER_LED;
    uint32_t phase_in_led = start_phase % WS2812_PHASES_PER_LED;
    uint32_t bit_idx      = phase_in_led / WS2812_PHASES_PER_BIT;
    uint32_t phase_in_bit = phase_in_led % WS2812_PHASES_PER_BIT;

    /* Access LED data as raw bytes in memory order — this automatically
     * sends bytes in the correct wire order for any WS2812_BYTE_ORDER,
     * matching the bitbang driver's behavior. */
    uint8_t *led_data    = (uint8_t *)&ws2812_leds[led_idx];
    uint32_t byte_idx    = bit_idx / 8;
    uint32_t bit_in_byte = 7 - (bit_idx % 8);  /* MSB first */
    uint8_t  color_byte  = led_data[byte_idx];
    uint8_t  bit_val     = (color_byte >> bit_in_byte) & 1;

    for (uint32_t i = 0; i < count; i++) {
        /* Encode phase */
        switch (phase_in_bit) {
            case 0:
                buf[i] = BSRR_SET;
                break;
            case 1:
                buf[i] = bit_val ? BSRR_NOP : BSRR_RESET;
                break;
            case 2:
                buf[i] = bit_val ? BSRR_RESET : BSRR_NOP;
                break;
        }

        /* Advance to next phase (increment, no division) */
        phase_in_bit++;
        if (phase_in_bit >= WS2812_PHASES_PER_BIT) {
            phase_in_bit = 0;
            bit_idx++;

            if (bit_idx >= WS2812_BITS_PER_LED) {
                bit_idx = 0;
                led_idx++;
                /* Guard: don't read past the LED array on the final iteration */
                if (led_idx >= WS2812_LED_COUNT) {
                    break;
                }
                byte_idx = 0;
                bit_in_byte = 7;
                led_data = (uint8_t *)&ws2812_leds[led_idx];
                color_byte = led_data[0];
            } else {
                uint32_t new_byte_idx = bit_idx / 8;
                if (new_byte_idx != byte_idx) {
                    byte_idx = new_byte_idx;
                    bit_in_byte = 7;
                    color_byte = led_data[byte_idx];
                } else {
                    bit_in_byte--;
                }
            }
            bit_val = (color_byte >> bit_in_byte) & 1;
        }
    }
}

/**
 * @brief Force-abort an in-progress DMA transfer (thread context only)
 *
 * Called from ws2812_flush() when a timeout is detected.
 * Stops DMA, stops timer, forces pin LOW, clears transfer flag.
 */
static void ws2812_abort_transfer(void) {
    dmaStreamDisable(ws2812_dma_stream);
    /* Clear stale raw status (TFR/ERR) from the aborted transfer.
     * Without this, re-enabling interrupt masks on the next frame would
     * fire a spurious TFR ISR that corrupts block sequencing.
     * (Normal completion doesn't need this — dmaServeInterrupt() clears
     * status at line 470 of wb32_dma.c after the callback returns.) */
    dmaStreamClearInterrupt(ws2812_dma_stream);
    gptStopTimer(ws2812_gpt);
    WS2812_GPIO_PORT->BSRR = BSRR_RESET;
    ws2812_transfer_active = false;
}

/**
 * @brief DMA callback — chain next block or stop
 *
 * Called from ISR context on TFR (transfer complete) or ERR.
 * On TFR: advance block index, start next DMA block if more remain.
 * On ERR or final block: stop timer, clear transfer_active.
 */
static void ws2812_dma_callback(void *p, uint32_t flags) {
    (void)p;

    if (ws2812_dma_stream == NULL) {
        return;
    }

    /* NOTE: Do NOT call dmaStreamClearInterrupt() here.
     * dmaServeInterrupt() checks each interrupt type individually and
     * calls this callback per-type, then clears ALL status at the end.
     * Clearing early would wipe ERR status before dmaServeInterrupt
     * checks it, silently losing simultaneous DMA errors.
     */

    /* Error — abort */
    if (flags & WB32_DMAC_IT_STATE_ERR) {
#ifdef WS2812_DEBUG
        ws2812_dma_error_count++;
#endif
        dmaStreamDisable(ws2812_dma_stream);
        gptStopTimerI(ws2812_gpt);
        WS2812_GPIO_PORT->BSRR = BSRR_RESET;
        ws2812_transfer_active = false;
        return;
    }

    /* TFR — block complete.
     * Guard: dmaServeInterrupt() checks TFR before ERR and calls
     * separately, so if both fired for the same block the ERR handler
     * above already aborted. Skip TFR processing in that case. */
    if ((flags & WB32_DMAC_IT_STATE_TFR) && ws2812_transfer_active) {
        ws2812_block_idx++;

        if (ws2812_block_idx < WS2812_BLOCK_COUNT) {
            /* More blocks — chain next DMA transfer */
            uint32_t buf_sel  = ws2812_block_idx % 4;
            uint32_t blk_size = ws2812_get_block_size(ws2812_block_idx);

            dmaStreamSetSource(ws2812_dma_stream, ws2812_buf[buf_sel]);
            dmaStreamSetTransactionSize(ws2812_dma_stream, blk_size);
            /* Prevent stale timer UIF from triggering an immediate DMA
             * transfer with wrong phase timing at block boundaries.
             * Interrupt-protected: CNT reset → SR clear → arm DMA → UDE enable
             * must complete within one timer period (30 ticks) to prevent
             * a timer overflow from setting UIF before UDE is re-enabled.
             *
             * NOTE: WB32 timer SR is rw (not rc_w0 like STM32). Writing
             * SR = ~FLAG can SET other bits. Always use SR = 0.
             */
            ws2812_gpt->tim->DIER &= ~WB32_TIM_DIER_UDE;
            __disable_irq();
            ws2812_gpt->tim->CNT = 0;
            ws2812_gpt->tim->SR = 0;
            dmaStreamEnable(ws2812_dma_stream);
            ws2812_gpt->tim->DIER |= WB32_TIM_DIER_UDE;
            __enable_irq();
        } else {
            /* All blocks sent — stop */
            dmaStreamDisable(ws2812_dma_stream);
            gptStopTimerI(ws2812_gpt);
            WS2812_GPIO_PORT->BSRR = BSRR_RESET;
            ws2812_transfer_active = false;
        }
    }
}

/**
 * @brief Initialize the GPIO DMA WS2812 driver
 */
void ws2812_init(void) {
    /* Configure GPIO pin as push-pull output */
    palSetLineMode(WS2812_DI_PIN, PAL_MODE_OUTPUT_PUSHPULL);
    palClearLine(WS2812_DI_PIN);  /* Start LOW */

    /* Start GPT driver */
    gptStart(ws2812_gpt, &ws2812_gpt_config);

    /* Allocate DMA with callback */
    ws2812_dma_stream = dmaStreamAlloc(WS2812_GPIO_DMA_STREAM - WB32_DMA_STREAM(0),
                                       2,  /* IRQ priority (high) */
                                       ws2812_dma_callback, NULL);

    if (ws2812_dma_stream == NULL) {
        return;
    }

    /* Configure DMA mode: Memory -> Peripheral (GPIO BSRR)
     * Non-circular, TFR interrupt for block chaining, error interrupt.
     */
    dmaStreamSetDestination(ws2812_dma_stream, &WS2812_GPIO_PORT->BSRR);
    dmaStreamSetMode(ws2812_dma_stream,
                     WB32_DMA_CHCFG_HWHIF(WS2812_GPIO_DMA_TIM_HWHIF) |
                     WB32_DMA_CHCFG_DIR_M2P |
                     WB32_DMA_CHCFG_PSIZE_WORD |
                     WB32_DMA_CHCFG_MSIZE_WORD |
                     WB32_DMA_CHCFG_MINC |
                     WB32_DMA_CHCFG_TCIE |    /* TFR interrupt for block chaining */
                     WB32_DMA_CHCFG_TEIE |    /* Error detection */
                     WB32_DMA_CHCFG_PL(3));   /* Highest DMA priority */
    /* NO CIRC — non-circular */
    /* NO HTIE — no half-transfer / BLOCK interrupt (doesn't work on WB32) */

    /* Workaround: dmaStreamSetMode() leaves HS_SEL_SRC=0 (hardware) in CFGL
     * for M2P transfers. The source is memory — it has no hardware handshake.
     * Without this fix, the DMA waits forever for a source request from
     * HWHIF 0 (TIM1_CH1) that never fires, causing an immediate lockup.
     */
    ws2812_dma_stream->dmac->Ch[ws2812_dma_stream->channel].CFGL |= WB32_DMAC_SRC_HIFS_SW;

    ws2812_transfer_active = false;
}

/**
 * @brief Check if a DMA transfer is currently in progress
 * @return true if transfer is active, false if idle
 */
bool ws2812_is_transfer_active(void) {
    return ws2812_transfer_active;
}

/**
 * @brief Set color for a single LED
 */
void ws2812_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index >= 0 && index < WS2812_LED_COUNT) {
        ws2812_leds[index].r = red;
        ws2812_leds[index].g = green;
        ws2812_leds[index].b = blue;
#ifdef WS2812_RGBW
        ws2812_rgb_to_rgbw(&ws2812_leds[index]);
#endif
    }
}

/**
 * @brief Set color for all LEDs
 */
void ws2812_set_color_all(uint8_t red, uint8_t green, uint8_t blue) {
    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        ws2812_set_color(i, red, green, blue);
    }
}

/**
 * @brief Flush LED colors to the strip
 *
 * Pre-fills double buffer, starts DMA, then feeds remaining blocks
 * from thread context as the ISR advances through them.
 */
void ws2812_flush(void) {
    if (ws2812_dma_stream == NULL) {
        return;
    }

    /* Wait for any previous transfer to complete, with safety timeout.
     * Normal frame takes ~3ms; timeout at WS2812_TIMEOUT_MS prevents permanent
     * lockup if DMA stalls (e.g. voltage sag, hardware glitch). */
    uint32_t start = timer_read32();
    while (ws2812_transfer_active) {
        if (timer_elapsed32(start) >= WS2812_TIMEOUT_MS) {
            ws2812_abort_transfer();
            break;
        }
    }

    /* Pre-fill all 4 buffers before starting DMA */
    uint32_t blk0_size = ws2812_get_block_size(0);
    ws2812_fill_block(ws2812_buf[0], 0, blk0_size);

    if (WS2812_BLOCK_COUNT > 1) {
        uint32_t blk1_size = ws2812_get_block_size(1);
        ws2812_fill_block(ws2812_buf[1], WS2812_BLOCK_SIZE, blk1_size);
    }

    if (WS2812_BLOCK_COUNT > 2) {
        uint32_t blk2_size = ws2812_get_block_size(2);
        ws2812_fill_block(ws2812_buf[2], 2 * WS2812_BLOCK_SIZE, blk2_size);
    }

    if (WS2812_BLOCK_COUNT > 3) {
        uint32_t blk3_size = ws2812_get_block_size(3);
        ws2812_fill_block(ws2812_buf[3], 3 * WS2812_BLOCK_SIZE, blk3_size);
    }

    /* Start transfer: block 0 from buf[0] */
    ws2812_block_idx = 0;
    ws2812_transfer_active = true;

    dmaStreamSetSource(ws2812_dma_stream, ws2812_buf[0]);
    dmaStreamSetTransactionSize(ws2812_dma_stream, blk0_size);
    /* Re-enable interrupt masks cleared by dmaStreamDisable() at end of previous frame */
    dmaStreamEnableInterrupt(ws2812_dma_stream, WB32_DMAC_IT_TFR);
    dmaStreamEnableInterrupt(ws2812_dma_stream, WB32_DMAC_IT_ERR);

    /* Suppress both UG-triggered and stale-handshake DMA at frame start:
     * 1. Disable UDE so gpt_lld_start_timer()'s EGR=UG can't generate DMA request
     * 2. Start timer (UG event fires harmlessly with UDE=0)
     * 3. With interrupts disabled: reset CNT → clear SR → arm DMA → re-enable UDE
     *    Interrupt protection prevents ISRs from delaying the sequence past one
     *    timer period (30 ticks), which would allow UIF to set before UDE enable.
     *
     * NOTE: WB32 timer SR is rw (not rc_w0 like STM32). Writing
     * SR = ~FLAG can SET other bits. Always use SR = 0.
     */
    ws2812_gpt->tim->DIER &= ~WB32_TIM_DIER_UDE;
    gptStartContinuous(ws2812_gpt, WS2812_PHASE_TICKS);
    chSysDisable();
    ws2812_gpt->tim->CNT = 0;
    ws2812_gpt->tim->SR = 0;
    dmaStreamEnable(ws2812_dma_stream);
    ws2812_gpt->tim->DIER |= WB32_TIM_DIER_UDE;
    chSysEnable();

    /* Feed remaining blocks from thread context.
     * ISR chains the next block from the pre-filled buffer;
     * we fill the just-released buffer with the block after that.
     * 4 buffers provide 3-block (630µs) runway which exceeds fill time (282µs),
     * eliminating the CPU/DMA data race for all 12 blocks.
     */
    uint32_t next_fill = 4;  /* Blocks 0, 1, 2, and 3 already filled */
    start = timer_read32();

    while (ws2812_transfer_active) {
        if (timer_elapsed32(start) >= WS2812_TIMEOUT_MS) {
            ws2812_abort_transfer();
            break;
        }

        uint32_t current = ws2812_block_idx;  /* volatile read */

        while (next_fill < WS2812_BLOCK_COUNT && current >= (next_fill - 3)) {
            uint32_t start_phase = next_fill * WS2812_BLOCK_SIZE;
            uint32_t size = ws2812_get_block_size(next_fill);
            ws2812_fill_block(ws2812_buf[next_fill % 4], start_phase, size);
            next_fill++;
        }
    }

    /* WS2812 reset pulse: hold data line low for ≥280µs.
     * Pin is already LOW (last DMA phase was RESET). Just wait.
     */
    chThdSleepMicroseconds(WS2812_RESET_US);
}

#endif /* WB32F3G71xx || WB32FQ95xx */
