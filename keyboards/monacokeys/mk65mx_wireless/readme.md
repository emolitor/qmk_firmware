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

### 1. ROW4 is not connected to the MCU

The ROW4 net (bottom row, 9 keys) is floating: the ROW4 global label was
never attached to a U1 pin and PA0 (pin 10) is left unconnected. The
firmware assigns ROW4 to A0, which requires a bodge wire from U1 pin 10
to the ROW4 net until the respin. The nearest ROW4 access point is the
D64 cathode pad in the spacebar area, about 26 mm from PA0.

### 2. RGB LED matrix polarity is inverted

The MHT151RGBCT is a common anode RGB LED (pins: 1 = common anode,
2/3/4 = B/G/R cathodes), but the board wires the common anodes to the
IS31FL3741A CS pins and the R/G/B cathodes to the SW pins. Per the
IS31FL3741A datasheet, CS1-CS39 are current *sinks* and SW1-SW9 are
high-side *sources*, so every LED is reverse biased on every scan cycle
and cannot light. No firmware workaround exists. Fix options:

* Populate a pin-compatible **common cathode** RGB LED in the same
  footprint; the existing routing then becomes a valid IS31FL3741A
  topology and the firmware's LED table works unchanged, or
* Rework the matrix for common anode parts (anode buses on SW rows,
  per-colour cathodes on CS columns) and regenerate the LED table.

The I2C interface, driver configuration, SDB control and the extracted
75-LED wiring map were all validated against the rev B01 board; the
firmware side is ready as soon as the LEDs can conduct.

### 3. J2 feeds VSYS directly

J2 pin 1 lands on the VSYS rail. Powering the board through J2 while
the main USB is connected puts two supplies in contention on VSYS, and
with a battery attached (SW1 on) an external 5 V on VSYS back-feeds the
battery through the charger's discharge FET, bypassing charge control.
Until the respin: use one cable at a time, and switch SW1 off whenever
J2 is powered.

## Rev B02 respin checklist

1. Attach the ROW4 global label to PA0 (U1 pin 10).
2. Swap LED1-LED75 to a pin-compatible common cathode RGB LED (or
   rework the LED matrix orientation for common anode parts).
3. Move J2 pin 1 from VSYS to the BQ24075 input (+5V net) through a
   Schottky diode (SS34, LCSC C8678, JLCPCB basic part): resolves the
   contention and battery back-feed by construction; the diode drop
   only affects charging while programming the CH592.

## Notes

* The USB PID 0x0002 is provisional.
* PC14/PC15 are used as matrix columns; the 32.768 kHz crystal on the
  board belongs to the CH592F, not the STM32.
* The 64K flash part uses the STM32U073x8 linker script; the last 2K
  sector holds the wear-leveling EEPROM backing store.
