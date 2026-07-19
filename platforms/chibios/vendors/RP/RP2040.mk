#
# Raspberry Pi RP2040 specific drivers and build glue
##############################################################################
COMMON_VPATH += $(PLATFORM_PATH)/$(PLATFORM_KEY)/$(DRIVER_DIR)/vendor/$(MCU_FAMILY)/$(MCU_SERIES)

# Activate the ChibiOS DMA and PIO subsystems for the QMK vendor drivers.
ifeq ($(strip $(WS2812_DRIVER)), vendor)
    OPT_DEFS += -DRP_DMA_REQUIRED=TRUE
endif
ifneq ($(filter vendor,$(strip $(WS2812_DRIVER)) $(strip $(SERIAL_DRIVER)) $(strip $(PS2_DRIVER))),)
    OPT_DEFS += -DRP_PIO_REQUIRED=TRUE
endif

#
# Startup (CRT0) configuration: single core, VTOR pointed at the vector table.
##############################################################################
ADEFS  += -DCRT0_VTOR_INIT=1 \
          -DCRT0_EXTRA_CORES_NUMBER=0

CFLAGS += -DNDEBUG

# The bootrom function/data lookup tables live at constant near-zero
# addresses; stop GCC's array-bounds analysis from treating those constant
# pointers as null-page accesses (same workaround the pico-sdk uses).
CFLAGS += --param=min-pagesize=0

#
# Multi-flash-chip second stage bootloaders, selected via RP2040_FLASH_*
# defines. The matching ChibiOS-bundled default boot2 is suppressed in
# mcu_selection.mk (RP2040_BOOT_STAGE2 :=).
##############################################################################
PLATFORM_RP2040_PATH := $(PLATFORM_PATH)/$(PLATFORM_KEY)/vendors/$(MCU_FAMILY)

PLATFORM_SRC += $(PLATFORM_RP2040_PATH)/stage2_bootloaders.c

EXTRAINCDIRS += $(PLATFORM_RP2040_PATH)
