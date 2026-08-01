# MonacoKeys MK65MX Wireless

A 65% hotswap keyboard built on the STM32U073C8 (64K flash), with a CH592F
BLE MCU, IS31FL3741A RGB matrix controller and BQ24075 battery charging.

This is the initial wired-only port: USB and the key matrix. Wireless
(CH592F over USART2 on A2/A3, wake on A1), RGB matrix (IS31FL3741A on I2C1
B6/B7, SDB on B5) and battery support are not yet enabled.

* Keyboard maintainer: [emolitor](https://github.com/emolitor)
* Hardware: MK65MXW rev B01 (`MK65MX/pcb-wireless`)

Make example for this keyboard (after setting up your build environment):

    make monacokeys/mk65mx_wireless:default

Flashing example for this keyboard:

    make monacokeys/mk65mx_wireless:default:flash

Enter the bootloader by holding the BOOT0 button while plugging in, or by
holding the top-left key (bootmagic) while plugging in.

## Hardware errata (rev B01)

The ROW4 net (bottom row, 9 keys) is not connected to the MCU in the rev
B01 schematic: the ROW4 global label was never attached to a U1 pin, and
PA0 (pin 10) is left unconnected. The firmware assigns ROW4 to A0, which
requires a bodge wire from U1 pin 10 to the ROW4 net (any of the D61-D69
cathodes) until the PCB is respun with the ROW4 label on PA0.

## Notes

* The USB PID 0x0002 is provisional.
* PC14/PC15 are used as matrix columns; the 32.768 kHz crystal on the
  board belongs to the CH592F, not the STM32.
* The 64K flash part uses the STM32U073x8 linker script; the last 2K
  sector holds the wear-leveling EEPROM backing store.
