// Copyright 2022 Marek Kraus (@gamelaster)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gpio.h"
#include <hal.h>
#include "ps2.h"
#include "debug.h"

#if !defined(MCU_RP)
#    error PIO Driver is only available for Raspberry Pi RP MCUs!
#endif

#if defined(PS2_ENABLE)
#    if defined(PS2_MOUSE_ENABLE)
#        if !defined(PS2_MOUSE_USE_REMOTE_MODE)
#            define BUFFERED_MODE_ENABLE
#        endif
#    else // PS2 Keyboard
#        define BUFFERED_MODE_ENABLE
#    endif
#endif

#if PS2_DATA_PIN + 1 == PS2_CLOCK_PIN
#    define PS2_FIRST_PIN PS2_DATA_PIN
#    define PS2_DATA_PINDIR_BIT 1
#    define PS2_CLOCK_PINDIR_BIT 2
#elif PS2_DATA_PIN - 1 == PS2_CLOCK_PIN
#    define PS2_FIRST_PIN PS2_CLOCK_PIN
#    define PS2_DATA_PINDIR_BIT 2
#    define PS2_CLOCK_PINDIR_BIT 1
#else
#    error PS/2 clock and data pin must be consecutive!
#endif

static void pio_serve_interrupt(void* param, uint32_t ints);

#if defined(PS2_PIO_USE_PIO1)
static const rp_pio_block_t* pio_block = RP_PIO1_BLOCK;
#else
static const rp_pio_block_t* pio_block = RP_PIO0_BLOCK;
#endif

#define PS2_WRAP_TARGET 0
#define PS2_WRAP 20

// clang-format off
static const uint16_t ps2_program_instructions[] = {
                                        //     .wrap_target
    0x00c7,                             //  0: jmp    pin, 7
    0xe02a,                             //  1: set    x, 10
    0x2000 | PS2_CLOCK_PIN,             //  2: wait   0 gpio, CLK
    0x4001,                             //  3: in     pins, 1
    0x2080 | PS2_CLOCK_PIN,             //  4: wait   1 gpio, CLK
    0x0042,                             //  5: jmp    x--, 2
    0x0000,                             //  6: jmp    0
    0x00e9,                             //  7: jmp    !osre, 9
    0x0000,                             //  8: jmp    0
    0xff80 | PS2_DATA_PINDIR_BIT,       //  9: set    pindirs, DATA         [31]
    0xe280,                             // 10: set    pindirs, 0             [2]
    0xe080 | PS2_CLOCK_PINDIR_BIT,      // 11: set    pindirs, CLK
    0x2000 | PS2_CLOCK_PIN,             // 12: wait   0 gpio, CLK
    0xe029,                             // 13: set    x, 9
    0x6081,                             // 14: out    pindirs, 1
    0x2080 | PS2_CLOCK_PIN,             // 15: wait   1 gpio, CLK
    0x2000 | PS2_CLOCK_PIN,             // 16: wait   0 gpio, CLK
    0x004e,                             // 17: jmp    x--, 14
    0xe083,                             // 18: set    pindirs, 3
    0x2000 | PS2_CLOCK_PIN,             // 19: wait   0 gpio, CLK
    0x2080 | PS2_CLOCK_PIN,             // 20: wait   1 gpio, CLK
                                        //     .wrap
};
// clang-format on

static const rp_pio_program_t ps2_program = {
    .instructions = ps2_program_instructions,
    .length       = 21,
    .origin       = -1,
};

static const rp_pio_sm_t* state_machine = NULL;
static thread_reference_t tx_thread     = NULL;

#define BUFFER_SIZE 32
static input_buffers_queue_t               pio_rx_queue;
static __attribute__((aligned(4))) uint8_t pio_rx_buffer[BQ_BUFFER_SIZE(BUFFER_SIZE, sizeof(uint32_t))];

uint8_t ps2_error = PS2_ERR_NONE;

static void pio_serve_interrupt(void* param, uint32_t ints) {
    if (ints & PIO_IRQ_RXNEMPTY(state_machine->smidx)) {
        osalSysLockFromISR();
        uint32_t* frame_buffer = (uint32_t*)ibqGetEmptyBufferI(&pio_rx_queue);
        if (frame_buffer == NULL) {
            osalSysUnlockFromISR();
            return;
        }
        *frame_buffer = pioSmGetX(state_machine);
        ibqPostFullBufferI(&pio_rx_queue, sizeof(uint32_t));
        osalSysUnlockFromISR();
    }

    if (ints & PIO_IRQ_TXNFULL(state_machine->smidx)) {
        pioSmDisableInterruptX(state_machine, PIO_IRQ_TXNFULL(state_machine->smidx));
        osalSysLockFromISR();
        osalThreadResumeI(&tx_thread, MSG_OK);
        osalSysUnlockFromISR();
    }
}

void ps2_host_init(void) {
    ibqObjectInit(&pio_rx_queue, false, pio_rx_buffer, sizeof(uint32_t), BUFFER_SIZE, NULL, NULL);

    // The allocation also releases the PIO block from reset and registers the
    // interrupt handler.
    state_machine = pioSmAlloc(pio_block, RP_PIO_SM_ID_ANY, CORTEX_MAX_KERNEL_PRIORITY, pio_serve_interrupt, NULL);
    if (state_machine == NULL) {
        dprintln("ERROR: Failed to acquire state machine for PS/2!");
        ps2_error = PS2_ERR_NODATA;
        return;
    }

    int32_t offset = pioProgramLoad(pio_block, &ps2_program);
    if (offset < 0) {
        dprintln("ERROR: Failed to load PS/2 PIO program!");
        ps2_error = PS2_ERR_NODATA;
        pioSmFree(state_machine);
        state_machine = NULL;
        return;
    }

    rp_pio_sm_config_t config;
    pioSmConfigDefaultX(&config);
    pioSmConfigSetWrapX(&config, (uint32_t)offset + PS2_WRAP_TARGET, (uint32_t)offset + PS2_WRAP);

    // Steady-state pin mapping: SET on both pins, OUT and IN on the data pin.
    pioSmConfigSetSetPinsX(&config, pioGpioToRel(pio_block, PS2_FIRST_PIN), 2U);
    pioSmConfigSetOutPinsX(&config, pioGpioToRel(pio_block, PS2_DATA_PIN), 1U);
    pioSmConfigSetInPinsX(&config, pioGpioToRel(pio_block, PS2_DATA_PIN));
    pioSmConfigSetJmpPinX(&config, pioGpioToRel(pio_block, PS2_CLOCK_PIN));

    // OUT shifts right with autopull at 10 bits, IN shifts right with
    // autopush at 11 bits.
    pioSmConfigSetOutShiftX(&config, true, true, 10U);
    pioSmConfigSetInShiftX(&config, true, true, 11U);
    pioSmConfigSetFrequencyX(&config, 200000U);

    pioSmInit(state_machine, (uint32_t)offset, &config);
    // Set pindirs of both pins to 1 (output enable is inverted below, so this
    // releases the open-drain lines).
    pioSmSetConsecutivePindirsX(state_machine, PS2_FIRST_PIN, 2U, true);

    // clang-format off
    iomode_t pin_mode = PAL_RP_PAD_IE |
                        PAL_RP_GPIO_OE |
                        PAL_RP_PAD_SLEWFAST |
                        PAL_RP_PAD_DRIVE12 |
                        // Invert output enable so that pindirs=1 means input
                        // and indirs=0 means output. This way, out pindirs
                        // works correctly with the open-drain PS/2 interface.
                        // Setting pindirs=1 effectively pulls the line high,
                        // due to the pull-up resistor, while pindirs=0 pulls
                        // the line low.
                        PAL_RP_IOCTRL_OEOVER_DRVINVPERI |
                        (pio_block->pioidx == 0 ? PAL_MODE_ALTERNATE_PIO0 : PAL_MODE_ALTERNATE_PIO1);
    // clang-format on

    palSetLineMode(PS2_DATA_PIN, pin_mode);
    palSetLineMode(PS2_CLOCK_PIN, pin_mode);

    pioSmEnableInterruptX(state_machine, PIO_IRQ_RXNEMPTY(state_machine->smidx));

    pioSmEnableX(state_machine);
}

static int bit_parity(int x) {
    return !__builtin_parity(x);
}

uint8_t ps2_host_send(uint8_t data) {
    uint32_t frame = 0b1000000000;
    frame          = frame | data;

    if (bit_parity(data)) {
        frame = frame | (1 << 8);
    }

    pioSmPutX(state_machine, frame);

    msg_t msg = MSG_OK;
    osalSysLock();
    while (pioSmIsTxFullX(state_machine)) {
        pioSmEnableInterruptX(state_machine, PIO_IRQ_TXNFULL(state_machine->smidx));
        msg = osalThreadSuspendTimeoutS(&tx_thread, TIME_MS2I(100));
        if (msg < MSG_OK) {
            pioSmDisableInterruptX(state_machine, PIO_IRQ_TXNFULL(state_machine->smidx));
            ps2_error = PS2_ERR_NODATA;
            osalSysUnlock();
            return 0;
        }
    }
    osalSysUnlock();

    return ps2_host_recv_response();
}

static uint8_t ps2_get_data_from_frame(uint32_t frame) {
    uint8_t  data       = (frame >> 22) & 0xFF;
    uint32_t start_bit  = (frame & 0b00000000001000000000000000000000) ? 1 : 0;
    uint32_t parity_bit = (frame & 0b01000000000000000000000000000000) ? 1 : 0;
    uint32_t stop_bit   = (frame & 0b10000000001000000000000000000000) ? 1 : 0;

    if (start_bit != 0) {
        ps2_error = PS2_ERR_STARTBIT1;
        return 0;
    }

    if (parity_bit != bit_parity(data)) {
        ps2_error = PS2_ERR_PARITY;
        return 0;
    }

    if (stop_bit != 1) {
        ps2_error = PS2_ERR_STARTBIT2;
        return 0;
    }

    return data;
}

uint8_t ps2_host_recv_response(void) {
    uint32_t frame = 0;
    msg_t    msg   = MSG_OK;

    msg = ibqReadTimeout(&pio_rx_queue, (uint8_t*)&frame, sizeof(uint32_t), TIME_MS2I(100));
    if (msg < MSG_OK) {
        ps2_error = PS2_ERR_NODATA;
        return 0;
    }

    return ps2_get_data_from_frame(frame);
}

#ifdef BUFFERED_MODE_ENABLE

bool pbuf_has_data(void) {
    osalSysLock();
    bool has_data = !ibqIsEmptyI(&pio_rx_queue);
    osalSysUnlock();
    return has_data;
}

uint8_t ps2_host_recv(void) {
    uint32_t frame = 0;
    msg_t    msg   = MSG_OK;

    uint8_t has_data = pbuf_has_data();
    if (has_data) {
        msg = ibqReadTimeout(&pio_rx_queue, (uint8_t*)&frame, sizeof(uint32_t), TIME_MS2I(100));
        if (msg < MSG_OK) {
            ps2_error = PS2_ERR_NODATA;
            return 0;
        }
    } else {
        ps2_error = PS2_ERR_NODATA;
    }

    return frame != 0 ? ps2_get_data_from_frame(frame) : 0;
}

#endif
