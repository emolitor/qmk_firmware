# Onekey RP2350

Supported Hardware: Raspberry Pi Pico 2 (RP2350A) in ARM Cortex-M33 mode.

To trigger keypress, short together pins *GP4* and *GP5*.

The same keyboard can be built for the Hazard3 RISC-V cores with:

    qmk compile -kb handwired/onekey/rp2350 -km default -e MCU=RP2350_RISCV

(or use `handwired/onekey/rp2350_riscv`, which selects RISC-V mode in
`keyboard.json`). The bootrom picks the matching architecture automatically
when the UF2 is flashed.
