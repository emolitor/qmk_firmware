#  Bridge75
The Bridge75 is based on a Westberry Tech WB32FQ95 MCU in a LQFP64 package
and can be programmed with wb32-dfu. The keyboard uses a WCH CH582F chip
running a proprietary firmware to provide wireless connectivity.


## Differences from the original firmware
It is desired to maintain the behaviour of the existing user experience while
updating the firmware. Like the original firmware the UX for the board is
indicated when in Layer 1 (FN pressed on default layout) however the battery
indicator is always shown in this mode. This is a difference from the original
firmware where the battery level was only shown when KC_BATQ (default FN+D)
was being pressed. This is due to the Westberry Wireless code maintaining the
battery state automatically and allowing the implementation to be simplified.


## Flashing a new firmware
Hold ESCAPE [0,0] to enter bootloader mode while inserting the USB cable into
the keyboard. Then run the following to flash the firmware.
```shell
qmk flash -kb shortcut/bridge75/ansi -km default
```


## Quirks
The Bridge75 uses a slightly different way to initialize the bluetooth device
names. As the wireless stack is initialized in `keyboard_post_init_kb` a call
is made to `md_send_devinfo` where the bluetooth name is passed but with the
`$` character as the last character. The `$` is replaced with the BT device
number when pairing. This seems to be a feature unique to the version of the
CH582F firmware that HFD uses. This also means during pairing do not 'CLEAN'
the device and do not update the device name. Simply send PAIR to
`md_send_devctrl`.


## How are the LEDs wired?
The LEDs are WS2812 the first of which is wired to PB10 and uses the bitbang
driver.


## Reference Material
The following shouldn't be upstreamed but I've included documentation relevant
for porting in the repo to have everything in a single place.
* [WB32FQ95 Data Sheet](../../../em-documentation/EN_DS1104041_WB32FQ95xC_V01.pdf)
* [WB32FQ95 Reference Manual](../../../em-documentation/EN_RM2905025_WB32FQ95xx_V01.pdf)
* [WCH CH582F Data Sheet](../../../em-documentation/CH583DS1.PDF)
* [Puya P25D80SH Data Sheet](../../../em-documentation/PUYA-P25Q80H-SSH-IT_C194872.pdf)
* [TP4056 Data Sheet](../../../em-documentation/TP4056.pdf)
