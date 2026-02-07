/**
 * @file lp_sleep.c
 * @brief Low power sleep mode implementation using PAL callbacks
 *        Based on bridge75wireless lp_sleep.c but adapted to use ChibiOS PAL
 * @author Joy chang.li@westberrytech.com
 * @version 1.0.0
 * @date 2022-09-16
 *
 * @copyright Copyright (c) 2022 Westberry Technology (ChangZhou) Corp., Ltd
 */

#ifdef ENTRY_STOP_MODE

#include "quantum.h"
#include "wireless.h"

#ifndef LPWR_UART_WAKEUP_DISABLE
#    include "uart.h"
#endif

// Magic value to mark wakeup from deep sleep (survives soft reset)
#define WAKEUP_MAGIC 0x5AA5

static ioline_t row_pins[MATRIX_ROWS] = MATRIX_ROW_PINS;
static ioline_t col_pins[MATRIX_COLS] = MATRIX_COL_PINS;

// clang-format off
static const uint32_t pre_lp_code[] = {553863175u, 554459777u, 1208378049u, 4026624001u, 688390415u, 554227969u, 3204472833u, 1198571264u, 1073807360u, 1073808388u};
#define PRE_LP() ((void (*)(void))((unsigned int)(pre_lp_code) | 0x01))()

static const uint32_t post_lp_code[] = {553863177u, 554459777u, 1208509121u, 51443856u, 4026550535u, 1745485839u, 3489677954u, 536895496u, 673389632u, 1198578684u, 1073807360u, 536866816u, 1073808388u};
#define POST_LP() ((void (*)(void))((unsigned int)(post_lp_code) | 0x01))()
// clang-format on

#if PAL_USE_CALLBACKS != TRUE
#    error PAL_USE_CALLBACKS must be set to TRUE!
#endif

#if !((DIODE_DIRECTION == ROW2COL) || (DIODE_DIRECTION == COL2ROW))
#    error DIODE_DIRECTION must be one of COL2ROW or ROW2COL!
#endif

extern void __early_init(void);
extern void matrix_init_pins(void);

void lp_recovery_hook(void);

// PAL callback for wakeup events
static void lp_palcallback(void *arg) {
#ifndef LPWR_UART_WAKEUP_DISABLE
    uint8_t line = (uint32_t)arg & 0xFF;
    if (line == PAL_PAD(UART_RX_PIN)) {
        lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_UART);
    } else
#endif
    {
        lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_MATRIX);
    }

    irqDeinit();
    EXTI->PR = 0xFFFFFFFF;
}

static void lp_pal_events_init(void) {
    for (uint8_t i = 0; i < 16; i++) {
        _pal_events[i].cb  = lp_palcallback;
        _pal_events[i].arg = (void *)(uint32_t)i;
    }
}

// Enable NVIC vector for a specific pad with the correct priority
static void lp_enable_nvic_for_pad(uint8_t pad) {
    switch (pad) {
        case 0:
            nvicEnableVector(EXTI0_IRQn, WB32_IRQ_EXTI0_PRIORITY);
            break;
        case 1:
            nvicEnableVector(EXTI1_IRQn, WB32_IRQ_EXTI1_PRIORITY);
            break;
        case 2:
            nvicEnableVector(EXTI2_IRQn, WB32_IRQ_EXTI2_PRIORITY);
            break;
        case 3:
            nvicEnableVector(EXTI3_IRQn, WB32_IRQ_EXTI3_PRIORITY);
            break;
        case 4:
            nvicEnableVector(EXTI4_IRQn, WB32_IRQ_EXTI4_PRIORITY);
            break;
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
            nvicEnableVector(EXTI9_5_IRQn, WB32_IRQ_EXTI5_9_PRIORITY);
            break;
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
            nvicEnableVector(EXTI15_10_IRQn, WB32_IRQ_EXTI10_15_PRIORITY);
            break;
    }
}

static void lp_exti_init(void) {
    lp_pal_events_init();

#if DIODE_DIRECTION == ROW2COL
    for (uint8_t i = 0; i < MATRIX_COLS; i++) {
        if (col_pins[i] != NO_PIN) {
            setPinOutputOpenDrain(col_pins[i]);
            writePinLow(col_pins[i]);
        }
    }

    for (uint8_t i = 0; i < MATRIX_ROWS; i++) {
        if (row_pins[i] != NO_PIN) {
            setPinInputHigh(row_pins[i]);
            waitInputPinDelay();
            palEnableLineEvent(row_pins[i], PAL_EVENT_MODE_BOTH_EDGES);
            lp_enable_nvic_for_pad(PAL_PAD(row_pins[i]));
        }
    }
#elif DIODE_DIRECTION == COL2ROW
    for (uint8_t i = 0; i < MATRIX_ROWS; i++) {
        if (row_pins[i] != NO_PIN) {
            setPinOutputOpenDrain(row_pins[i]);
            writePinLow(row_pins[i]);
        }
    }

    for (uint8_t i = 0; i < MATRIX_COLS; i++) {
        if (col_pins[i] != NO_PIN) {
            setPinInputHigh(col_pins[i]);
            waitInputPinDelay();
            palEnableLineEvent(col_pins[i], PAL_EVENT_MODE_BOTH_EDGES);
            lp_enable_nvic_for_pad(PAL_PAD(col_pins[i]));
        }
    }
#endif

#ifndef LPWR_UART_WAKEUP_DISABLE
    setPinInput(UART_RX_PIN);
    waitInputPinDelay();
    palEnableLineEvent(UART_RX_PIN, PAL_EVENT_MODE_BOTH_EDGES);
    lp_enable_nvic_for_pad(PAL_PAD(UART_RX_PIN));
#endif

    /* IRQ subsystem initialization.*/
    irqInit();
}

static void stop_mode_entry(void) {
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;

    /* Clear pending interrupts */
    EXTI->PR = 0x7FFFF;
    for (uint8_t i = 0; i < 8; i++) {
        for (uint8_t j = 0; j < 32; j++) {
            if (NVIC->ISPR[i] & (0x01UL < j)) {
                NVIC->ICPR[i] = (0x01UL < j);
            }
        }
    }
    SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk; // Clear Systick IRQ Pending

    /* Clear all bits except DBP and FCLKSD bit */
    PWR->CR0 &= 0x09U;

    // STOP LP4 MODE S32KON
    PWR->CR0 |= 0x3B004U;
    PWR->CFGR = 0x3B3;

    PRE_LP();

    /* Set SLEEPDEEP bit of Cortex System Control Register */
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

    /* Request Wait For Interrupt */
    __WFI();

    POST_LP();

    /* Clear SLEEPDEEP bit of Cortex System Control Register */
    SCB->SCR &= (~SCB_SCR_SLEEPDEEP_Msk);

    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
}

static void lp_clock_enable(void) {
#ifdef WB32_WAKE_RESET_HACK
    NVIC_SystemReset();
#endif

    __early_init();

    rccEnableEXTI();

#if WB32_SERIAL_USE_UART1
    rccEnableUART1();
#endif
#if WB32_SERIAL_USE_UART2
    rccEnableUART2();
#endif
#if WB32_SERIAL_USE_UART3
    rccEnableUART3();
#endif
#if WB32_SPI_USE_QSPI
    rccEnableQSPI();
#endif
#if WB32_SPI_USE_SPIM2
    rccEnableSPIM2();
#endif
#if WB32_I2C_USE_I2C1
    rccEnableI2C1();
#endif
#if WB32_I2C_USE_I2C2
    rccEnableI2C2();
#endif
#if WB32_GPT_USE_TIM1 || WB32_ICU_USE_TIM1 || WB32_PWM_USE_TIM1
    rccEnableTIM1();
#endif
#if WB32_ST_USE_TIM2 || WB32_GPT_USE_TIM2 || WB32_ICU_USE_TIM2 || WB32_PWM_USE_TIM2
    rccEnableTIM2();
#endif
#if WB32_ST_USE_TIM3 || WB32_GPT_USE_TIM3 || WB32_ICU_USE_TIM3 || WB32_PWM_USE_TIM3
    rccEnableTIM3();
#endif
#if WB32_ST_USE_TIM4 || WB32_GPT_USE_TIM4 || WB32_ICU_USE_TIM4 || WB32_PWM_USE_TIM4
    rccEnableTIM4();
#endif

#ifndef LPWR_UART_WAKEUP_DISABLE
    palSetLineMode(UART_RX_PIN, PAL_MODE_ALTERNATE(UART_RX_PAL_MODE) | PAL_OUTPUT_TYPE_PUSHPULL | PAL_OUTPUT_SPEED_HIGHEST);
#endif

    lp_recovery_hook();
}

void lp_system_sleep(void) {
    lpwr_set_sleep_wakeupcd(LPWR_WAKEUP_NONE);

    // Mark that we're entering deep sleep - used by bootmagic_scan to skip
    // the bootmagic check on wake for faster restart
    PWR->GPREG0 = WAKEUP_MAGIC;

    chSysLock();
    lp_exti_init();
    chSysUnlock();

    chSysDisable();
    stop_mode_entry();
    lp_clock_enable();
    matrix_init_pins();
    chSysEnable();
}

__WEAK void lp_recovery_hook(void) {
    /*
     * User implementation
     * related configuration and clock recovery
     */
}

#endif /* ENTRY_STOP_MODE */
