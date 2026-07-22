// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later

#include "serial_usart.h"
#include "serial_protocol.h"
#include <hal.h>
#include "wait.h"
#include "debug.h"

#if !defined(MCU_RP)
#    error PIO Driver is only available for Raspberry Pi RP MCUs!
#endif

static inline bool receive_impl(uint8_t* destination, const size_t size, sysinterval_t timeout);
static inline bool send_impl(const uint8_t* source, const size_t size);
static void        pio_serve_interrupt(void* param, uint32_t ints);

#define MSG_PIO_ERROR ((msg_t)(-3))

#if defined(SERIAL_PIO_USE_PIO1)
static const rp_pio_block_t* pio_block = RP_PIO1_BLOCK;
#else
static const rp_pio_block_t* pio_block = RP_PIO0_BLOCK;
#endif

#define UART_TX_WRAP_TARGET 0
#define UART_TX_WRAP 3

// clang-format off
#if defined(SERIAL_USART_FULL_DUPLEX)
static const uint16_t uart_tx_program_instructions[] = {
            //     .wrap_target
    0x9fa0, //  0: pull   block           side 1 [7]
    0xf727, //  1: set    x, 7            side 0 [7]
    0x6001, //  2: out    pins, 1
    0x0642, //  3: jmp    x--, 2                 [6]
            //     .wrap
};
#else
static const uint16_t uart_tx_program_instructions[] = {
            //     .wrap_target
    0x9fa0, //  0: pull   block           side 1 [7]
    0xf727, //  1: set    x, 7            side 0 [7]
    0x6081, //  2: out    pindirs, 1
    0x0642, //  3: jmp    x--, 2                 [6]
            //     .wrap
};
#endif
// clang-format on

static const rp_pio_program_t uart_tx_program = {
    .instructions = uart_tx_program_instructions,
    .length       = 4,
    .origin       = -1,
};

#define UART_RX_WRAP_TARGET 0
#define UART_RX_WRAP 8

// clang-format off
static const uint16_t uart_rx_program_instructions[] = {
            //     .wrap_target
    0x2020, //  0: wait   0 pin, 0
    0xea27, //  1: set    x, 7                   [10]
    0x4001, //  2: in     pins, 1
    0x0642, //  3: jmp    x--, 2                 [6]
    0x00c8, //  4: jmp    pin, 8
    0xc020, //  5: irq    wait 0
    0x20a0, //  6: wait   1 pin, 0
    0x0000, //  7: jmp    0
    0x8020, //  8: push   block
            //     .wrap
};
// clang-format on

static const rp_pio_program_t uart_rx_program = {
    .instructions = uart_rx_program_instructions,
    .length       = 9,
    .origin       = -1,
};

thread_reference_t rx_thread = NULL;
thread_reference_t tx_thread = NULL;

static const rp_pio_sm_t* tx_sm = NULL;
static const rp_pio_sm_t* rx_sm = NULL;

// Steady-state PINCTRL values, restored after executing 'set' instructions
// which need a temporary SET pin mapping.
static uint32_t tx_pinctrl = 0;
static pin_t    active_tx_pin;

static void pio_serve_interrupt(void* param, uint32_t ints) {
    // The RX FIFO is not empty any more, therefore wake any sleeping rx thread
    if (ints & PIO_IRQ_RXNEMPTY(rx_sm->smidx)) {
        // Disable rx not empty interrupt
        pioSmDisableInterruptX(rx_sm, PIO_IRQ_RXNEMPTY(rx_sm->smidx));

        osalSysLockFromISR();
        osalThreadResumeI(&rx_thread, MSG_OK);
        osalSysUnlockFromISR();
    }

    // The TX FIFO is not full any more, therefore wake any sleeping tx thread
    if (ints & PIO_IRQ_TXNFULL(tx_sm->smidx)) {
        // Disable tx not full interrupt
        pioSmDisableInterruptX(tx_sm, PIO_IRQ_TXNFULL(tx_sm->smidx));

        osalSysLockFromISR();
        osalThreadResumeI(&tx_thread, MSG_OK);
        osalSysUnlockFromISR();
    }

    // PIO irq flag 0 is raised on framing or break errors by the rx state
    // machine
    if (ints & PIO_IRQ_SM(0)) {
        pio_block->pio->IRQ = 1U << 0;

        osalSysLockFromISR();
        osalThreadResumeI(&rx_thread, MSG_PIO_ERROR);
        osalSysUnlockFromISR();
    }
}

/**
 * @brief Execute a 'set pins'/'set pindirs' style instruction on a state
 * machine which is mapped to a single SET pin, restoring the steady-state
 * pin mapping afterwards.
 */
static void pio_sm_exec_set(const rp_pio_sm_t* smp, pin_t pin, uint16_t instruction, uint32_t steady_pinctrl) {
    pioSmSetPinctrlX(smp, (1U << PIO_SM_PINCTRL_SET_COUNT_Pos) | (pioGpioToRel(pio_block, pin) << PIO_SM_PINCTRL_SET_BASE_Pos));
    pioSmExecX(smp, instruction);
    pioSmSetPinctrlX(smp, steady_pinctrl);
}

// PIO 'set pins'/'set pindirs' opcodes with a single mapped pin.
#define PIO_INSTR_SET_PINS_LOW 0xE000U
#define PIO_INSTR_SET_PINS_HIGH 0xE001U
#define PIO_INSTR_SET_PINDIRS_IN 0xE080U
#define PIO_INSTR_SET_PINDIRS_OUT 0xE081U

#if !defined(SERIAL_USART_FULL_DUPLEX)
// clang-format off
#define TX_PIN_HALF_DUPLEX_MODE(drive) (PAL_RP_PAD_IE |             \
                                        PAL_RP_GPIO_OE |            \
                                        PAL_RP_PAD_SCHMITT |        \
                                        PAL_RP_PAD_PUE |            \
                                        PAL_RP_PAD_SLEWFAST |       \
                                        (drive) |                   \
                                        PAL_RP_IOCTRL_OEOVER_DRVINVPERI | \
                                        (pio_block->pioidx == 0 ? PAL_MODE_ALTERNATE_PIO0 : PAL_MODE_ALTERNATE_PIO1))
// clang-format on

// The internal pull-ups of the RP2040 are rather weakish with a range of 50k to
// 80k, which in turn do not provide enough current to guarantee fast signal rise
// times with a parasitic capacitance of greater than 100pf. In real world
// applications, like split keyboards which might have vias in the signal path
// or long PCB traces, this prevents a successful communication. The solution
// is to temporarily augment the weak pull ups from the receiving side by
// driving the tx pin high. On the receiving side the lowest possible drive
// strength is chosen because the transmitting side must still be able to drive
// the signal low. With this configuration the rise times are fast enough and
// the generated low level with 360mV will generate a logical zero.
static void enter_rx_state(void) {
    osalSysLock();
    // Wait for the transmitting state machines FIFO to run empty. At this point
    // the last byte has been pulled from the transmitting state machines FIFO
    // into the output shift register. We have to wait a tiny bit more until
    // this byte is transmitted, before we can turn on the receiving state
    // machine again.
    while (!pioSmIsTxEmptyX(tx_sm)) {
    }
    // Wait for ~11 bits, 1 start bit + 8 data bits + 1 stop bit + 1 bit
    // headroom.
    wait_us(1000000U * 11U / SERIAL_USART_SPEED);
    // Disable tx state machine to not interfere with our tx pin manipulation
    pioSmDisableX(tx_sm);
    palSetLineMode(active_tx_pin, TX_PIN_HALF_DUPLEX_MODE(PAL_RP_PAD_DRIVE2));
    pio_sm_exec_set(tx_sm, active_tx_pin, PIO_INSTR_SET_PINS_HIGH, tx_pinctrl);
    pio_sm_exec_set(tx_sm, active_tx_pin, PIO_INSTR_SET_PINDIRS_IN, tx_pinctrl);
    pioSmEnableX(rx_sm);
    osalSysUnlock();
}

static void leave_rx_state(void) {
    osalSysLock();
    // In Half-duplex operation the tx pin dual-functions as sender and
    // receiver. To not receive the data we will send, we disable the receiving
    // state machine.
    pioSmDisableX(rx_sm);
    pio_sm_exec_set(tx_sm, active_tx_pin, PIO_INSTR_SET_PINDIRS_OUT, tx_pinctrl);
    pio_sm_exec_set(tx_sm, active_tx_pin, PIO_INSTR_SET_PINS_LOW, tx_pinctrl);
    palSetLineMode(active_tx_pin, TX_PIN_HALF_DUPLEX_MODE(PAL_RP_PAD_DRIVE12));
    pioSmRestartX(tx_sm);
    pioSmEnableX(tx_sm);
    osalSysUnlock();
}
#else
// All this trickery is gladly not necessary for full-duplex.
static inline void enter_rx_state(void) {}
static inline void leave_rx_state(void) {}
#endif

/**
 * @brief Clear the FIFO of the RX state machine.
 */
inline void serial_transport_driver_clear(void) {
    osalSysLock();
    while (!pioSmIsRxEmptyX(rx_sm)) {
        pioSmClearFifosX(rx_sm);
    }
    osalSysUnlock();
}

static inline msg_t sync_tx(sysinterval_t timeout) {
    msg_t msg = MSG_OK;
    osalSysLock();
    while (pioSmIsTxFullX(tx_sm)) {
        pioSmEnableInterruptX(tx_sm, PIO_IRQ_TXNFULL(tx_sm->smidx));
        msg = osalThreadSuspendTimeoutS(&tx_thread, timeout);
        if (msg < MSG_OK) {
            pioSmDisableInterruptX(tx_sm, PIO_IRQ_TXNFULL(tx_sm->smidx));
            break;
        }
    }
    osalSysUnlock();
    return msg;
}

static inline bool send_impl(const uint8_t* source, const size_t size) {
    size_t send = 0;
    msg_t  msg;
    while (send < size) {
        msg = sync_tx(TIME_MS2I(SERIAL_USART_TIMEOUT));
        if (msg < MSG_OK) {
            return false;
        }

        osalSysLock();
        while (send < size) {
            if (pioSmIsTxFullX(tx_sm)) {
                break;
            }
            if (send >= size) {
                break;
            }
            pioSmPutX(tx_sm, (uint32_t)(*source));
            source++;
            send++;
        }
        osalSysUnlock();
    }

    return send == size;
}

/**
 * @brief Blocking send of buffer with timeout.
 *
 * @return true Send success.
 * @return false Send failed.
 */
inline bool serial_transport_send(const uint8_t* source, const size_t size) {
    leave_rx_state();
    bool result = send_impl(source, size);
    enter_rx_state();

    return result;
}

static inline msg_t sync_rx(sysinterval_t timeout) {
    msg_t msg = MSG_OK;
    osalSysLock();
    while (pioSmIsRxEmptyX(rx_sm)) {
        pioSmEnableInterruptX(rx_sm, PIO_IRQ_RXNEMPTY(rx_sm->smidx));
        msg = osalThreadSuspendTimeoutS(&rx_thread, timeout);
        if (msg < MSG_OK) {
            pioSmDisableInterruptX(rx_sm, PIO_IRQ_RXNEMPTY(rx_sm->smidx));
            break;
        }
    }
    osalSysUnlock();
    return msg;
}

static inline bool receive_impl(uint8_t* destination, const size_t size, sysinterval_t timeout) {
    size_t read = 0U;

    while (read < size) {
        msg_t msg = sync_rx(timeout);
        if (msg < MSG_OK) {
            return false;
        }
        osalSysLock();
        while (true) {
            if (pioSmIsRxEmptyX(rx_sm)) {
                break;
            }
            if (read >= size) {
                break;
            }
            // The RX shift register pushes MSB-aligned bytes.
            *destination++ = (uint8_t)(pioSmGetX(rx_sm) >> 24);
            read++;
        }
        osalSysUnlock();
    }

    return read == size;
}

/**
 * @brief  Blocking receive of size * bytes with timeout.
 *
 * @return true Receive success.
 * @return false Receive failed, e.g. by timeout.
 */
inline bool serial_transport_receive(uint8_t* destination, const size_t size) {
    return receive_impl(destination, size, TIME_MS2I(SERIAL_USART_TIMEOUT));
}

/**
 * @brief  Blocking receive of size * bytes.
 *
 * @return true Receive success.
 * @return false Receive failed.
 */
inline bool serial_transport_receive_blocking(uint8_t* destination, const size_t size) {
    return receive_impl(destination, size, TIME_INFINITE);
}

static inline bool pio_tx_init(pin_t tx_pin) {
    int32_t offset = pioProgramLoad(pio_block, &uart_tx_program);
    if (offset < 0) {
        return false;
    }

    active_tx_pin = tx_pin;

    uint32_t rel = pioGpioToRel(pio_block, tx_pin);

    rp_pio_sm_config_t config;
    pioSmConfigDefaultX(&config);
    pioSmConfigSetWrapX(&config, (uint32_t)offset + UART_TX_WRAP_TARGET, (uint32_t)offset + UART_TX_WRAP);

    // Steady-state pin mapping: both OUT and side-set drive the tx pin,
    // because sometimes we need to assert user data onto the pin (with OUT)
    // and sometimes assert constant values (start/stop bit, via side-set).
    pioSmConfigSetOutPinsX(&config, rel, 1U);
    pioSmConfigSetSidesetPinsX(&config, rel);

#if defined(SERIAL_USART_FULL_DUPLEX)
    // Optional side-set that asserts logic levels.
    pioSmConfigSetSidesetX(&config, 2U, true, false);
#else
    // Optional side-set that changes pin directions instead of logic levels.
    pioSmConfigSetSidesetX(&config, 2U, true, true);
#endif

    // OUT shifts to right, no autopull, TX FIFO joined for full depth.
    pioSmConfigSetOutShiftX(&config, true, false, 32U);
    pioSmConfigSetFifoJoinX(&config, RP_PIO_FIFO_JOIN_TX);
    // SM transmits 1 bit per 8 execution cycles.
    pioSmConfigSetFrequencyX(&config, 8U * SERIAL_USART_SPEED);

    // Steady-state PINCTRL, restored by pio_sm_exec_set() after temporary
    // SET pin mappings.
    tx_pinctrl = config.pinctrl;

#if defined(SERIAL_USART_FULL_DUPLEX)
    // clang-format off
    iomode_t tx_pin_mode = PAL_RP_GPIO_OE |
                           PAL_RP_PAD_SLEWFAST |
                           PAL_RP_PAD_DRIVE4 |
                           (pio_block->pioidx == 0 ? PAL_MODE_ALTERNATE_PIO0 : PAL_MODE_ALTERNATE_PIO1);
    // clang-format on
    pio_sm_exec_set(tx_sm, tx_pin, PIO_INSTR_SET_PINS_HIGH, tx_pinctrl);
    pio_sm_exec_set(tx_sm, tx_pin, PIO_INSTR_SET_PINDIRS_OUT, tx_pinctrl);
#else
    iomode_t tx_pin_mode = TX_PIN_HALF_DUPLEX_MODE(PAL_RP_PAD_DRIVE12);
    pio_sm_exec_set(tx_sm, tx_pin, PIO_INSTR_SET_PINS_LOW, tx_pinctrl);
    pio_sm_exec_set(tx_sm, tx_pin, PIO_INSTR_SET_PINDIRS_OUT, tx_pinctrl);
#endif

    palSetLineMode(tx_pin, tx_pin_mode);

    pioSmInit(tx_sm, (uint32_t)offset, &config);
    pioSmEnableX(tx_sm);
    return true;
}

static inline bool pio_rx_init(pin_t rx_pin) {
    int32_t offset = pioProgramLoad(pio_block, &uart_rx_program);
    if (offset < 0) {
        return false;
    }

#if defined(SERIAL_USART_FULL_DUPLEX)
    pio_sm_exec_set(rx_sm, rx_pin, PIO_INSTR_SET_PINDIRS_IN, 0U);
    // clang-format off
    iomode_t rx_pin_mode = PAL_RP_PAD_IE |
                           PAL_RP_PAD_SCHMITT |
                           PAL_RP_PAD_PUE |
                           (pio_block->pioidx == 0 ? PAL_MODE_ALTERNATE_PIO0 : PAL_MODE_ALTERNATE_PIO1);
    // clang-format on
    palSetLineMode(rx_pin, rx_pin_mode);
#endif

    uint32_t rel = pioGpioToRel(pio_block, rx_pin);

    rp_pio_sm_config_t config;
    pioSmConfigDefaultX(&config);
    pioSmConfigSetWrapX(&config, (uint32_t)offset + UART_RX_WRAP_TARGET, (uint32_t)offset + UART_RX_WRAP);
    pioSmConfigSetInPinsX(&config, rel);
    pioSmConfigSetJmpPinX(&config, rel);
    // IN shifts to right, autopush disabled, RX FIFO joined for full depth.
    pioSmConfigSetInShiftX(&config, true, false, 32U);
    pioSmConfigSetFifoJoinX(&config, RP_PIO_FIFO_JOIN_RX);
    // SM samples 1 bit per 8 execution cycles.
    pioSmConfigSetFrequencyX(&config, 8U * SERIAL_USART_SPEED);

    pioSmInit(rx_sm, (uint32_t)offset, &config);
    pioSmEnableX(rx_sm);
    return true;
}

static inline void pio_uart_init(pin_t tx_pin, pin_t rx_pin) {
    // The allocations also release the PIO block from reset and register the
    // interrupt handler. As the pio implementation is timing critical we use
    // the highest possible priority; it is applied on the first allocation of
    // the block. The handler is registered only once -- it serves all state
    // machines of this driver.
    tx_sm = pioSmAlloc(pio_block, RP_PIO_SM_ID_ANY, CORTEX_MAX_KERNEL_PRIORITY, NULL, NULL);
    if (tx_sm == NULL) {
        dprintln("ERROR: Failed to acquire state machine for serial transmission!");
        return;
    }

    rx_sm = pioSmAlloc(pio_block, RP_PIO_SM_ID_ANY, CORTEX_MAX_KERNEL_PRIORITY, pio_serve_interrupt, NULL);
    if (rx_sm == NULL) {
        dprintln("ERROR: Failed to acquire state machine for serial reception!");
        return;
    }

    if (!pio_tx_init(tx_pin)) {
        dprintln("ERROR: Failed to load serial transmission PIO program!");
        return;
    }
    if (!pio_rx_init(rx_pin)) {
        dprintln("ERROR: Failed to load serial reception PIO program!");
        return;
    }

    // Enable rx not empty, tx not full and rx error interrupt sources. The
    // FIFO level sources are re-enabled on demand by the sync functions.
    pioSmEnableInterruptX(rx_sm, PIO_IRQ_RXNEMPTY(rx_sm->smidx) | PIO_IRQ_TXNFULL(tx_sm->smidx) | PIO_IRQ_SM(0));

    enter_rx_state();
}

/**
 * @brief PIO driver specific initialization function for the master side.
 */
void serial_transport_driver_master_init(void) {
#if defined(SERIAL_USART_FULL_DUPLEX)
    pin_t tx_pin = SERIAL_USART_TX_PIN;
    pin_t rx_pin = SERIAL_USART_RX_PIN;
#else
    pin_t tx_pin = SERIAL_USART_TX_PIN;
    pin_t rx_pin = SERIAL_USART_TX_PIN;
#endif

#if defined(SERIAL_USART_PIN_SWAP)
    pio_uart_init(rx_pin, tx_pin);
#else
    pio_uart_init(tx_pin, rx_pin);
#endif
}

/**
 * @brief PIO driver specific initialization function for the slave side.
 */
void serial_transport_driver_slave_init(void) {
#if defined(SERIAL_USART_FULL_DUPLEX)
    pin_t tx_pin = SERIAL_USART_TX_PIN;
    pin_t rx_pin = SERIAL_USART_RX_PIN;
#else
    pin_t tx_pin = SERIAL_USART_TX_PIN;
    pin_t rx_pin = SERIAL_USART_TX_PIN;
#endif

    pio_uart_init(tx_pin, rx_pin);
}
