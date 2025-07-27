WIRELESS_ENABLE ?= yes
WIRELESS_DIR = $(TOP_DIR)/keyboards/shortcut/wireless

ifeq ($(strip $(WIRELESS_ENABLE)), yes)
    OPT_DEFS += -DWIRELESS_ENABLE -DNO_USB_STARTUP_CHECK
	BATTERY_DRIVER_REQUIRED = yes
	BATTERY_DRIVER = custom
	BLUETOOTH_ENABLE = yes
	BLUETOOTH_DRIVER = custom

    UART_DRIVER_REQUIRED ?= yes
    WIRELESS_LPWR_STOP_ENABLE ?= yes

    VPATH += $(WIRELESS_DIR)

    SRC += \
	    $(WIRELESS_DIR)/wb_bluetooth.c \
        $(WIRELESS_DIR)/lowpower.c \
        $(WIRELESS_DIR)/smsg.c \
        $(WIRELESS_DIR)/module.c
endif
