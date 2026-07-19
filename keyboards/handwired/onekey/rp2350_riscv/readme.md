# Onekey RP2350 RISC-V

Supported Hardware: Raspberry Pi Pico 2 (RP2350A) running on the Hazard3
RISC-V cores.

To trigger keypress, short together pins *GP4* and *GP5*.

See `handwired/onekey/rp2350` for the ARM Cortex-M33 variant of the same
hardware; the bootrom picks the matching architecture automatically when the
UF2 is flashed.
