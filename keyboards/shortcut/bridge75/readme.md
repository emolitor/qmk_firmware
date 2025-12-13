#  Bridge75
The Bridge75 is based on a Westberry Tech WB32FQ95 MCU in a LQFP64 package
and can be programmed with wb32-dfu. The keyboard uses a WCH CH582F chip
running a proprietary firmware to provide wireless connectivity.


## Differences from the original firmware
It is desired to maintain the behaviour of the existing user experience while
updating the firmware. However there are a few changes that have been made as
necessary to simplify the implementation or to avoid redundant features which
are support by QMK.

* The battery indicator is always shown when FN is pressed. When the keyboard
wakes up from sleep or is turned on the battery indicator will not show for
a few moments until the battery status is reported by the battery driver.
* The option to disable RGB was removed as it is redundant. Turn down the RGB
brigtness to zero if this is desired.
* The option to keep RGB always on when in USB mode has been removed. This
will be added back at some point in the future.
* When in MAC mode the keyboard HUD is BlUE, when in WIN[default] mode the
keyboard HUD is YELLOW.
* Some of the RGB settings may have changed due to changes in QMK lighting.
* This firmware has a deep sleep fix which restarts the keyboard when it comes
out of sleep. This results in a pause when waking from sleep which drops some
key presses.


## Flashing a new firmware
Hold ESCAPE [0,0] to enter bootloader mode while inserting the USB cable into
the keyboard. Then run the following to flash the firmware.
```shell
qmk flash -kb shortcut/bridge75/ansi -km default
```


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
