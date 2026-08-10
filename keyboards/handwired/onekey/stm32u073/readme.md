# STM32U073 onekey

Onekey for a custom, crystal-less STM32U073 board. The core runs at 56 MHz
from HSI16 via the PLL; USB is clocked from HSI48 with CRS autotrim against
the USB SOF.

To trigger keypress, short together pins *A0* and *A1*.

Enter the ROM DFU bootloader by holding BOOT0 high during reset. A blank
device boots straight into the ROM bootloader.

The STM32U073 remains in DFU mode after programming. Reset or power-cycle the
board after `dfu-util` reports that the file downloaded successfully.
