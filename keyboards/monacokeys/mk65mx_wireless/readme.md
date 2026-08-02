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

### 4. Inter-MCU UART is not crossed

The STM32-CH592 UART is wired TX-to-TX / RX-to-RX (U1 PA2/USART2_TX to
U2 PA8/RXD1's partner pin and vice versa are swapped). The CH592
cannot swap its UART pins; B01 firmware must set the STM32 USART2
CR2.SWAP bit (PA2 becomes RX, PA3 becomes TX) or the link is dead,
with possible push-pull contention if both transmitters are enabled
naively. Any stock serial configuration fails silently.

### 5. Battery charging is disabled (ILIM floating)

The BQ24075 ILIM pin (U4 pin 12) is deliberately no-connected, but the
datasheet requires a 1.1 k-8 k resistor to VSS in every mode: "Leaving
ILIM unconnected disables all charging." The power path works, so the
board runs normally on USB and discharges the battery when unplugged -
it just never charges, and the CHG LED never lights. B01 rework: bodge
a 1.2 k resistor from U4 pad 12 to GND.

## Rev B02 respin checklist

1. Attach the ROW4 global label to PA0 (U1 pin 10).
2. Swap LED1-LED75 to a pin-compatible common cathode RGB LED (or
   rework the LED matrix orientation for common anode parts).
3. Move J2 pin 1 from VSYS to the BQ24075 input (+5V net) through a
   Schottky diode (SS34, LCSC C8678, JLCPCB basic part): resolves the
   contention and battery back-feed by construction; the diode drop
   only affects charging while programming the CH592.
4. Fit R_ILIM (1.1 k-1.6 k, 0402) from BQ24075 ILIM (pin 12) to GND -
   without it charging is entirely disabled (erratum 5).
5. Re-strap the input current limit: EN2 = high, EN1 = low, so the
   limit is set by R_ILIM (~1.4 A) instead of USB500; the current
   EN1-to-VSYS bootstrap caps input at 500 mA, which the programmed
   ~494 mA charge current plus the LED load cannot share (the battery
   discharges while plugged in with the backlight up). If 500 mA hosts
   must be honored, add 0-ohm strap options or route EN1/EN2 to spare
   U1 GPIOs for firmware control.
6. Restore charge safety backstops: unground TMR (float for default
   timers or fit 18 k-72 k), and consider a real NTC contact on the
   battery harness instead of the fixed 10 k on TS.
7. Take the battery current path out of SW1 (50 mA-rated slide switch
   currently carries ~0.5 A charge and full discharge current): wire
   J1 pin 1 directly to BAT and repurpose SW1 onto BQ24075 SYSOFF
   (pin 15, currently hard-grounded) as a signal-level power switch.
8. Add battery voltage sensing: gated high-impedance divider (e.g.
   1 M/1 M + 100 nF behind a P-FET) from BATTERY to U1 PB11, so
   firmware can report battery level over BLE and warn on low charge.
9. Route charger status to the MCU: PGOOD (U4 pin 7) to U1 PB12 and
   CHG (U4 pin 9 net) to U1 PB13 via ~100 k pull-ups, giving firmware
   USB-present and charging/charged detection.
10. Replace the XC6206P332 LDO with an HT7833 (3.3 V, 500 mA, 4-7 uA
    quiescent, roughly half the dropout, similar cost; SOT-89 or
    SOT-23-5 footprint change). Keeps the standby budget while buying
    ~150 mV of low-battery headroom.
11. Grow PVCC bulk near U3 (add 22-47 uF) for 75-LED scan transients.
12. Cross the inter-MCU UART (erratum 4) and rename the nets
    directionally (e.g. STM_TX_CH_RX) so the roles are unambiguous.
13. Add a minimal testpoint set: VSYS, BATTERY, +3V3, +5V, ROW4, both
    UART lines, CHWAKE, SDB.
14. Tie the J2 mounting pads and the SW1 shield to GND (J1 already
    is); assign or remove the four floating vias.
15. Confirm the gasket-mount/no-mounting-holes decision against the
    case CAD before tooling.

Spare pin allocation with the above: PA0 = ROW4, PB11 = battery sense,
PB12 = PGOOD, PB13 = CHG, PB14/PF0 = testpoints.

## Notes

* The USB PID 0x0002 is provisional.
* PC14/PC15 are used as matrix columns; the 32.768 kHz crystal on the
  board belongs to the CH592F, not the STM32.
* The 64K flash part uses the STM32U073x8 linker script; the last 2K
  sector holds the wear-leveling EEPROM backing store.
