# OpenController Bench

A NUCLEO-U083RC driving an OpenController CH592F module over UART, for
validating the QMK side of the OpenController protocol without a keyboard PCB.
The driver sources are shared with `monacokeys/mk65mx_wireless`; this board
only adds the bench plumbing.

* Keyboard maintainer: [emolitor](https://github.com/emolitor)
* Hardware: NUCLEO-U083RC, CH592F module built with the OpenController
  `opencontroller-ch592` profile (UART1 remapped to PB12/PB13)

## Wiring

This is not the MK65MX wiring. The MK65MX PCB runs the link on USART2
(PA2/PA3, with the USART swap for its B01 erratum) and its module on the
`mk65mx-wireless-ch592` profile (UART1 on PA8/PA9). On the Nucleo, PA2/PA3
are the ST-Link VCP lines, and the bench module is a WeAct core board on the
`opencontroller-ch592` profile (UART1 remapped to PB12/PB13). Use USART1:

| Nucleo pin | Morpho | CH592F (WeAct label) | Role |
|---|---|---|---|
| PA9 (USART1_TX) | CN10 pin 21 | PB12 (B12, RXD1) | host to module; the module's wake edge |
| PA10 (USART1_RX) | CN10 pin 33 | PB13 (B13, TXD1) | module to host |
| GND | | G | |

On the WeAct board B12 and B13 sit on the bottom header row between B14 (the
debug pin) and B11. The WCH-LinkE on the module's debug pin has its own CDC
UART pins; leave them unconnected, otherwise its TX fights the Nucleo's.

### Do not wire the module to PA2/PA3

PA2/PA3 are routed to the ST-Link's virtual COM port through solder bridges.
The ST-Link's push-pull TX output sits on PA3 and holds it high; the module's
TX (a 5 mA driver on the CH592F) cannot pull the line down, so the STM32 sees
every frame it sends acknowledged by nothing and the driver loops on retries.
The symptom is `tx_ack_timeouts` climbing with zero bytes ever received. The
only redeeming feature of that wiring is that the VCP port on the host sees a
copy of everything the STM32 transmits, which is useful for timing the wake
preamble. If you lift the VCP TX solder bridge for that, build with
`BENCH_UART=2` to put the link on USART2 (PA2/PA3):

    make handwired/opencontroller_bench:default BENCH_UART=2

### Module profile pins

| OpenController profile | Module | UART1 RX (wake edge) | UART1 TX |
|---|---|---|---|
| `opencontroller-ch592` (bench) | WeAct CH592F core board | PB12 | PB13 |
| `mk65mx-wireless-ch592` | MK65MX module | PA8 | PA9 |

The bench validates the driver against the first profile. The wake contract is
the same on both, but the second has only been exercised through the MK65MX.

## Build and flash

    make handwired/opencontroller_bench:default
    python3 keyboards/handwired/opencontroller_bench/bench.py flash

Flashing goes through the on-board ST-Link with OpenOCD (`openocd.cfg`); the
board has no user USB connector, so there is no DFU and no HID console.

## Driving the bench

There is one physical key, the B1 USER button, and seven virtual keys that
`bench.py` presses by writing a mask over SWD:

| Key | Keycode | Purpose |
|---|---|---|
| 0 | `KC_F24` | B1 USER or virtual bit 0: a harmless report |
| 1 | `KC_CAPS` | end-to-end oracle: the host answers with its LED state (`5A` frame) |
| 2 | `OC_SLEEP` | explicit module sleep |
| 3 | `OC_PAIR` | fresh 2.4 GHz pairing |
| 4 | `OC_2G4` | select the radio |
| 5 | `OC_USB` | select USB (the module goes idle) |
| 6 | `OC_AUTO` | automatic routing |
| 7 | `OC_UNPAIR` | erase the bond |

    bench.py status            driver state, link state, diagnostics
    bench.py tap 0             press and release key 0
    bench.py keys 0x00         release everything
    bench.py mark 3            put a marker in the trace
    bench.py trace --follow    decoded live trace

The firmware keeps the last 1024 events in RAM: every frame written to the
UART, every byte received, every driver state change, key changes and marks,
each with a millisecond timestamp. `bench.py trace` decodes them.

`OPENCONTROLLER_SLEEP_TIMEOUT_MS` is set to 20 s here so the idle sleep can
be watched without waiting ten minutes.

## Validation ledger

Measured 2026-09-04 with a meter in series with the module's 3V3 supply,
OpenController `main` at 40bfb45 on a WeAct CH592F core board, CH572D
OpenDongle, the driver from `monacokeys/mk65mx_wireless` unchanged.

| Stage | Module current | Notes |
|---|---|---|
| Awake, connected | 7.5 mA | reference 7.4 mA |
| Awake, RF idle, auto-sleep off | 0.996 mA | reference 0.65 mA; not the 1.6 mA of the HSE-bias regression |
| Same, after a deep-sleep wake | 0.997 mA | no post-wake elevation |
| Auto-sleep floor (USB routing) | 5 µA | reached within 100 ms of the last frame |
| Explicit-sleep floor, auto-sleep off | 5 to 6 µA | held 60 s; one brief blip to 126 µA (the module's RTC housekeeping wake) |

Behaviour, 20 automated sleep/wake cycles with Caps Lock as the wake key:

* Every cycle: link torn down, wake preamble, first frame ACKed, link back in
  76 to 177 ms (mean 95 ms), one press report, one release, host LED toggled
  exactly once. No ACK timeouts, checksum errors, spurious ACKs or module
  reboots across the session.
* A key still held when the link returns is delivered once by the resync; a
  tap released before that (about 100 ms) is lost. Keys after the first need
  no preamble.
* A key held while sleep is requested defers the sleep; the sleep follows the
  release behind the release barrier.

Not measured: the bonded search with auto-sleep armed (dongle removed),
reference 0.85 mA average. USB suspend has no equivalent on this board.

Bench gotcha: macOS ignores Caps Lock presses shorter than roughly 100 ms, so
hold the virtual key for 500 ms (`bench.py tap 1 500`) when using the LED as
the delivery oracle; with a 150 ms hold and a 100 ms reconnect the host sees
too short a press and the LED does not move.

