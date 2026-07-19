# Raspberry Pi RP2350

The following table shows the current driver status for peripherals on RP2350 MCUs:

| System                                                        | Support                                          |
| ------------------------------------------------------------- | ------------------------------------------------ |
| [ADC driver](drivers/adc)                                     | :heavy_check_mark: (RP2350A channel mapping)     |
| [Audio](features/audio)                                       | :heavy_check_mark:                               |
| [Backlight](features/backlight)                               | :heavy_check_mark:                               |
| [I2C driver](drivers/i2c)                                     | :heavy_check_mark:                               |
| [SPI driver](drivers/spi)                                     | :heavy_check_mark:                               |
| [WS2812 driver](drivers/ws2812)                               | :heavy_check_mark: using `PIO` driver            |
| [External EEPROMs](drivers/eeprom)                            | :heavy_check_mark: using `I2C` driver            |
| [EEPROM emulation](drivers/eeprom#wear_leveling-configuration) | :heavy_check_mark:                               |
| [serial driver](drivers/serial)                               | :heavy_check_mark: using `SIO` or `PIO` driver   |
| [UART driver](drivers/uart)                                   | :heavy_check_mark: using `SIO` driver            |

## Architecture selection

The RP2350 contains both ARM Cortex-M33 and Hazard3 RISC-V cores; QMK can be
built for either. The `processor` key in `keyboard.json` selects the default
architecture:

* `RP2350` — ARM Cortex-M33 mode (UF2 family `RP2350_ARM_S`)
* `RP2350_RISCV` — Hazard3 RISC-V mode (UF2 family `RP2350_RISCV`)

Any RP2350 keyboard can be built for the other architecture at compile time,
without changes to the keyboard definition:

```
qmk compile -kb <keyboard> -km <keymap> -e MCU=RP2350_RISCV
```

The RP2350 bootrom selects the matching architecture automatically when the
UF2 is flashed, and firmware for both architectures can be flashed
interchangeably.

Building for the Hazard3 cores requires a RISC-V toolchain; QMK looks for
`riscv-none-elf-gcc` (the [xPack GNU RISC-V Embedded GCC](https://xpack-dev-tools.github.io/riscv-none-elf-gcc-xpack/)
naming), then `riscv32-unknown-elf-gcc` and `riscv64-unknown-elf-gcc`. The
compiler must understand the Hazard3 ISA string
(`rv32imac_zba_zbb_zbs_zbkb_zcb_zcmp_zicsr_zifencei`), which needs GCC 13 or
newer.

## Bootloader

Set `bootloader` to `rp2350` in `keyboard.json`. `QK_BOOT` (and the double-tap
feature below) enter the BOOTSEL USB mass-storage bootloader in both
architecture modes.

### Double-tap reset boot

The double-tap behavior known from RP2040 boards is available with the
following defines:

```c
#define RP2350_BOOTLOADER_DOUBLE_TAP_RESET // Activates the double-tap behavior
#define RP2350_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 200U // Timeout window in ms in which the double tap can occur.
#define RP2350_BOOTLOADER_DOUBLE_TAP_RESET_LED GP25 // Specify a optional status LED which blinks when entering the bootloader
```

The token is kept in the watchdog scratch registers, which survive a warm
reset on both architectures.

## Notes

* Both architecture modes currently run single-core.
* There is no second-stage bootloader (`boot2`) on the RP2350; the PICOBIN
  `IMAGE_DEF` metadata block embedded by the ChibiOS startup files replaces
  it.
* RP2350B (QFN-80) packages with GPIO banks above 31, and the second TIMER
  instance, are not yet supported; boards must use the RP2350A (QFN-60)
  pinout. `RP2350B_QFN80` support (including PIO `GPIOBASE` handling and the
  shifted ADC channel mapping) is a documented follow-up.
* Flash size defaults to 4MB (Raspberry Pi Pico 2). Override with
  `RP_FLASH_SIZE` when your board differs.
