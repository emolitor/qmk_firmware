# Copyright 2026 Eric Molitor (@emolitor)
# SPDX-License-Identifier: GPL-2.0-or-later

# The U083RC has 256K of flash and 40K of RAM; ChibiOS ships its script.
MCU_LDSCRIPT = STM32U083xC

# One physical key (B1 USER) plus seven virtual keys injected over SWD.
CUSTOM_MATRIX = lite
SRC += matrix.c bench.c

# The OpenController driver is shared with monacokeys/mk65mx_wireless.
BLUETOOTH_ENABLE = yes
BLUETOOTH_DRIVER = custom
UART_DRIVER_REQUIRED = yes
OPT_DEFS += -DOPENCONTROLLER_ENABLE -DOPENCONTROLLER_TRACE
# The link is on USART1 (PA9/PA10) by default. BENCH_UART=2 puts it on the
# ST-Link VCP pins PA2/PA3 instead; see readme.md before doing that.
ifeq ($(strip $(BENCH_UART)),2)
    OPT_DEFS += -DOPENCONTROLLER_BENCH_VCP_UART
endif
VPATH += keyboards/monacokeys/mk65mx_wireless
SRC += keyboards/monacokeys/mk65mx_wireless/opencontroller.c
SRC += keyboards/monacokeys/mk65mx_wireless/opencontroller_protocol.c
