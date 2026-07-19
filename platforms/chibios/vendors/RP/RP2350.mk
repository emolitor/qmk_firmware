#
# Raspberry Pi RP2350 specific drivers and build glue
##############################################################################
# The PIO-based vendor drivers are shared across the RP family.
COMMON_VPATH += $(PLATFORM_PATH)/$(PLATFORM_KEY)/$(DRIVER_DIR)/vendor/RP/common

# Activate the ChibiOS DMA and PIO subsystems for the QMK vendor drivers.
ifeq ($(strip $(WS2812_DRIVER)), vendor)
    OPT_DEFS += -DRP_DMA_REQUIRED=TRUE
endif
ifneq ($(filter vendor,$(strip $(WS2812_DRIVER)) $(strip $(SERIAL_DRIVER)) $(strip $(PS2_DRIVER))),)
    OPT_DEFS += -DRP_PIO_REQUIRED=TRUE
endif

#
# Startup (CRT0) configuration: single core on both architectures.
##############################################################################
ifeq ($(strip $(MCU)), risc-v)
    # Hazard3 mode: no FPU. The RISCV-HAZARD3 rules.mk (unused by QMK)
    # force-defines RISCV_USE_FPU=FALSE for C and assembly -- replicate that
    # here.
    ADEFS    += -DCRT0_EXTRA_CORES_NUMBER=0 \
                -DRISCV_USE_FPU=FALSE
    OPT_DEFS += -DRISCV_USE_FPU=FALSE
else
    # Cortex-M33 mode: point VTOR at our vector table.
    ADEFS += -DCRT0_VTOR_INIT=1 \
             -DCRT0_EXTRA_CORES_NUMBER=0
endif

CFLAGS += -DNDEBUG

# The bootrom function/data lookup tables live at constant near-zero
# addresses; stop GCC's array-bounds analysis from treating those constant
# pointers as null-page accesses (same workaround the pico-sdk uses).
CFLAGS += --param=min-pagesize=0

# Expose the RP family GP* pin definitions (_pin_defs.h).
EXTRAINCDIRS += $(PLATFORM_PATH)/$(PLATFORM_KEY)/vendors/$(MCU_FAMILY)

# No second stage bootloader on the RP2350: the PICOBIN IMAGE_DEF embedded
# block from the ChibiOS startup files replaces the RP2040 boot2 mechanism.
