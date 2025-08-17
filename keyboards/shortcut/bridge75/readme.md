#  Bridge75
This is an example keyboard implementation with the Bridge75 variants to show
how to use the [Westberry Wireless](https://github.com/emolitor/qmk_modules)
QMK Module to implement wireless support for the Westberry Technology wireless
submodule.

In order to compile this you need to use the complimentary 
[QMK Userspace for the prototype.](https://github.com/emolitor/qmk_userspace_via/tree/wireless-prototype-module)
with the `wireless-prototype-module` branch.

## Default Keymap
The keymap use the QMK Bluetooth/Wireless key codes implemented as follows.

| Key | Binding | Description |
| --- | ------- | ----------- |
| ~(ANSI) ¬(ISO) | OU_USB| Select USB |
| 1 | BT_PRF1 | Select BT 1 |
| 2 | BT_PRF2 | Select BT 2 |
| 3 | BT_PRF3 | Select BT 3 |
| 4 | BT_PRF4 | Select BT 4 |
| 5 | BT_PRF5 | Select BT 5 |
| 0 | OU_2P4G | Select 2.4G |

