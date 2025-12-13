# QwertyKeys QK65V2 Classic
The QK65V2 Classic is based on a Westberry Tech WB32FQ95 MCU in a LQFP64 
package and can be programmed with wb32-dfu. The keyboard uses a WCH CH582F 
chip running a proprietary firmware to provide wireless connectivity. This is 
a community port which was derived from a partial source originally released 
by the manufacturer and reverse engineered by probing the hardware directly.


## Flashing a new firmware
Hold ESCAPE [0,0] to enter bootloader mode while inserting the USB cable into
the keyboard. Then run the following to flash the firmware.
```shell
qmk flash -kb qwertykeys/qk65v2classic -km default
```


## Reference Material
The following shouldn't be upstreamed but I've included documentation relevant
for porting in the repo to have everything in a single place.
* [WB32FQ95 Data Sheet](../../../em-documentation/EN_DS1104041_WB32FQ95xC_V01.pdf)
* [WB32FQ95 Reference Manual](../../../em-documentation/EN_RM2905025_WB32FQ95xx_V01.pdf)
* [WCH CH582F Data Sheet](../../../em-documentation/CH583DS1.PDF)
* [Puya P25D80SH Data Sheet](../../../em-documentation/PUYA-P25Q80H-SSH-IT_C194872.pdf)
* [TP4056 Data Sheet](../../../em-documentation/TP4056.pdf)
