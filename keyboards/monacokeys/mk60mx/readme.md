# MK60MX

The MK60MX is a 60% RP2040 mechanical keyboard PCB with per-key RGB Matrix lighting and underglow.

* Keyboard Maintainer: [MonacoKeys](https://monacokeys.de/)
* Hardware Supported: MonacoKeys MK60MX RP2040 PCB
* Hardware Availability: [MonacoKeys MK60MX](https://monacokeys.de/en/products/mk60mx-60-mechanical-keyboard-pcb)

Make example for this keyboard (after setting up your build environment):

    make monacokeys/mk60mx:default
    make monacokeys/mk60mx:via

Flashing example for this keyboard:

    make monacokeys/mk60mx:via:flash

To enter the bootloader, use one of these methods:

* Hold the Bootmagic key (Escape) while connecting the keyboard.
* Press the physical reset button on the PCB.
* Press Fn+Escape in either supplied keymap.

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand-new QMK users should start with the [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).
