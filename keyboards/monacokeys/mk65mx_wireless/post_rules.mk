# Copyright 2026 Eric Molitor (@emolitor)
# SPDX-License-Identifier: GPL-2.0-or-later

ifeq ($(strip $(OPENCONTROLLER_ENABLE)), yes)
    BLUETOOTH_ENABLE := yes
    BLUETOOTH_DRIVER := custom
    UART_DRIVER_REQUIRED := yes
    OPT_DEFS += -DOPENCONTROLLER_ENABLE
    SRC += opencontroller.c opencontroller_protocol.c
endif
