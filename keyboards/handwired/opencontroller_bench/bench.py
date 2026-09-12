#!/usr/bin/env python3
# Copyright 2026 Eric Molitor (@emolitor)
# SPDX-License-Identifier: GPL-2.0-or-later
"""Drive the OpenController bench firmware over SWD.

The firmware exposes its state in a handful of RAM globals (see bench.h). This
tool reads and writes them through OpenOCD's TCL port, so nothing on the
keyboard side needs USB. It starts OpenOCD itself when none is listening.

    bench.py status                  driver state and diagnostics
    bench.py keys 0x04               set the virtual key mask
    bench.py tap 2 [hold_ms]         press and release virtual key 2
    bench.py mark 7                  label the trace with step 7
    bench.py raw a6 11 b7            send a frame behind the driver's back
    bench.py diag                    OpenController's A6 71 counter dump (refused while connected)
    bench.py trace [--follow]        decode the in-RAM event ring
    bench.py flash [elf]             program, verify and reset
    bench.py reset                   reset and run
    bench.py stop                    stop the OpenOCD this tool started

Virtual keys, from keymaps/default: 0 F24 (also the B1 button), 1 CAPS LOCK,
2 OC_SLEEP, 3 OC_PAIR, 4 OC_2G4, 5 OC_USB, 6 OC_AUTO, 7 OC_UNPAIR.
"""

import argparse
import os


import re
import socket
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True  # keeps __pycache__ out of the keyboard directory
import time

HERE = os.path.dirname(os.path.abspath(__file__))
QMK_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DEFAULT_ELF = os.path.join(QMK_ROOT, ".build", "handwired_opencontroller_bench_default.elf")
DEFAULT_CFG = os.path.join(HERE, "openocd.cfg")
PID_FILE = os.path.join(tempfile.gettempdir(), "opencontroller_bench_openocd.pid")

TRACE_CAPACITY = 1024
EVENT_SIZE = 16
STATUS_SIZE = 36

EV_TX, EV_RX, EV_STATE, EV_KEYS, EV_MARK, EV_RAWTX = 1, 2, 3, 4, 5, 6

CAPABILITY = ["UNKNOWN", "PENDING", "UNSUPPORTED", "READY"]
AUTOSLEEP = ["OFF", "ARMING", "ARMED"]
MODULE = ["AWAKE", "SLEEP_REQUESTED", "ASLEEP"]
LINK = ["UNKNOWN", "PAIRING", "CONNECTED", "DISCONNECTED", "RECONNECTING", "REJECTED"]
HOST = ["AUTO", "NONE", "USB", "BLUETOOTH", "2P4GHZ"]  # connection_host_t order
DIAG_FIELDS = ["rx_checksum_errors", "rx_partial_timeouts", "rx_ack_overflows", "rx_spurious_acks", "tx_ack_timeouts", "control_queue_overflows", "wake_preambles"]

CONTROL_NAMES = {
    0x11: "SELECT_USB",
    0x30: "SELECT_2G4",
    0x51: "PAIR",
    0x52: "UNPAIR",
    0x53: "BATTERY?",
    0x54: "SLEEP_NOW",
    0x56: "SLEEP_UNLOCK",
    0x57: "AUTOSLEEP_ARM",
    0x63: "FACTORY_PAIR",
    0x70: "VERSION?",
    0x81: "OTA",
}
STATUS_NAMES = {
    0x21: "BATTERY_LOW",
    0x22: "BATTERY_CRITICAL",
    0x23: "BATTERY_NORMAL",
    0x31: "PAIRING",
    0x32: "CONNECTED",
    0x33: "DISCONNECTED",
    0x34: "TRANSPORT_SELECTED",
    0x35: "RECONNECTING",
    0x36: "REJECTED",
    0x37: "SLEEP_READY",
}


def name_of(table, value):
    return table[value] if 0 <= value < len(table) else f"?{value}"


class OpenOCD:
    """Minimal client for OpenOCD's TCL RPC server (port 6666)."""

    def __init__(self, cfg, host="127.0.0.1", port=6666):
        self.cfg = cfg
        self.host = host
        self.port = port
        self.sock = None

    def _try_connect(self):
        try:
            self.sock = socket.create_connection((self.host, self.port), timeout=2.0)
            return True
        except OSError:
            self.sock = None
            return False

    def connect(self):
        if self._try_connect():
            return
        proc = subprocess.Popen(["openocd", "-f", self.cfg], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with open(PID_FILE, "w") as f:
            f.write(str(proc.pid))
        for _ in range(50):
            time.sleep(0.1)
            if proc.poll() is not None:
                sys.exit(f"openocd exited with {proc.returncode}; run it by hand to see why: openocd -f {self.cfg}")
            if self._try_connect():
                return
        sys.exit("openocd did not open its TCL port")

    def cmd(self, command, timeout=10.0):
        self.sock.settimeout(timeout)
        self.sock.sendall(command.encode() + b"\x1a")
        chunks = []
        while True:
            data = self.sock.recv(4096)
            if not data:
                raise RuntimeError("openocd closed the connection")
            chunks.append(data)
            if data.endswith(b"\x1a"):
                break
        return b"".join(chunks)[:-1].decode(errors="replace")

    def read_words(self, addr, count):
        text = self.cmd(f"mdw 0x{addr:08x} {count}")
        words = []
        for line in text.splitlines():
            m = re.match(r"0x[0-9a-fA-F]+:\s+(.*)", line)
            if m:
                words.extend(int(w, 16) for w in m.group(1).split())
        if len(words) != count:
            raise RuntimeError(f"short read at 0x{addr:08x}: {text!r}")
        return words

    def read_bytes(self, addr, size):
        path = os.path.join(tempfile.gettempdir(), f"opencontroller_bench_{os.getpid()}.bin")
        self.cmd(f"dump_image {path} 0x{addr:08x} {size}", timeout=30.0)
        with open(path, "rb") as f:
            data = f.read()
        os.unlink(path)
        if len(data) != size:
            raise RuntimeError(f"dump_image returned {len(data)} of {size} bytes")
        return data

    def write_byte(self, addr, value):
        self.cmd(f"mwb 0x{addr:08x} 0x{value & 0xFF:02x}")

    def write_word(self, addr, value):
        self.cmd(f"mww 0x{addr:08x} 0x{value & 0xFFFFFFFF:08x}")


def load_symbols(elf):
    out = subprocess.check_output(["arm-none-eabi-nm", elf], text=True)
    symbols = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    needed = ["bench_virtual_keys", "bench_mark", "bench_status", "bench_trace", "bench_trace_head", "bench_raw_tx"]
    missing = [n for n in needed if n not in symbols]
    if missing:
        sys.exit(f"{elf}: missing symbols {missing}; is this the bench firmware?")
    return symbols


def read_status(ocd, symbols):
    """Read bench_status consistently: seq must be even and unchanged across the dump.

    The firmware only republishes on a change, so this normally succeeds first
    time; during a burst of transitions it falls back to the last read and says so.
    """
    base = symbols["bench_status"]
    raw = None
    for _ in range(8):
        before = ocd.read_words(base + 32, 1)[0]
        raw = ocd.read_bytes(base, STATUS_SIZE)
        after = struct.unpack_from("<I", raw, 32)[0]
        if before == after and (after & 1) == 0:
            return raw
    print("warning: bench_status was changing during the read; values may be torn", file=sys.stderr)
    return raw


def decode_status(raw):
    t_ms, cap, auto, module, link, gen, leds, last, host, vkeys, row, _res = struct.unpack_from("<IBBBBHBBBBBB", raw, 0)
    diag = struct.unpack_from("<7H", raw, 16)
    return {
        "t_ms": t_ms,
        "capability": name_of(CAPABILITY, cap),
        "autosleep": name_of(AUTOSLEEP, auto),
        "module": name_of(MODULE, module),
        "link": name_of(LINK, link),
        "generation": gen,
        "leds": leds,
        "last_status": f"0x{last:02X} {STATUS_NAMES.get(last, '')}".strip(),
        "host": name_of(HOST, host),
        "virtual_keys": f"0x{vkeys:02X}",
        "matrix_row": f"0x{row:02X}",
        "diagnostics": dict(zip(DIAG_FIELDS, diag)),
    }


def describe_frame(data, direction):
    if not data:
        return "(empty)"
    head = data[0]
    hexs = " ".join(f"{b:02X}" for b in data)
    if head == 0xA6 and len(data) == 3:
        return f"{hexs}  {CONTROL_NAMES.get(data[1], 'CONTROL')}"
    if head == 0xA1 and len(data) == 10:
        report = data[1:9]
        keys = [f"{k:02X}" for k in report[2:] if k]
        mods = report[0]
        body = " ".join(keys) if keys else "release"
        if mods:
            body = f"mods={mods:02X} {body}"
        return f"{hexs}  REPORT {body}"
    if head == 0x61 and len(data) == 3:
        return f"{hexs}  ACK"
    if head == 0x5B and len(data) == 3:
        return f"{hexs}  STATUS {STATUS_NAMES.get(data[1], '?')}"
    if head == 0x5A and len(data) == 3:
        return f"{hexs}  LEDS 0x{data[1]:02X}"
    if head == 0x5C and len(data) == 3:
        return f"{hexs}  BATTERY {data[1]}%"
    if head == 0x00 and len(data) == 1 and direction == "TX":
        return f"{hexs}  WAKE PREAMBLE"
    return hexs


def split_rx(data):
    """RX events hold whatever arrived in one millisecond; re-frame by header."""
    frames, i = [], 0
    while i < len(data):
        if data[i] in (0x5A, 0x5B, 0x5C, 0x61) and i + 3 <= len(data):
            frames.append(data[i : i + 3])
            i += 3
        else:
            frames.append(data[i : i + 1])
            i += 1
    return frames


def describe_event(kind, data):
    if kind == EV_TX:
        return "TX  " + describe_frame(data, "TX")
    if kind == EV_RX:
        return "RX  " + " | ".join(describe_frame(f, "RX") for f in split_rx(data))
    if kind == EV_STATE:
        gen = data[4] | (data[5] << 8)
        return (
            f"ST  cap={name_of(CAPABILITY, data[0])} auto={name_of(AUTOSLEEP, data[1])} "
            f"module={name_of(MODULE, data[2])} link={name_of(LINK, data[3])} gen={gen} "
            f"leds={data[6]:02X} last=0x{data[7]:02X} host={name_of(HOST, data[8])} ack_timeouts={data[9]}"
        )
    if kind == EV_KEYS:
        return f"KEY row=0x{data[0]:02X}"
    if kind == EV_MARK:
        return f"MARK {struct.unpack('<I', bytes(data[:4]))[0]}"
    if kind == EV_RAWTX:
        return "RAW " + describe_frame(data, "TX") + "  (behind the driver)"
    return f"?{kind} " + " ".join(f"{b:02X}" for b in data)


def read_trace(ocd, symbols):
    head = ocd.read_words(symbols["bench_trace_head"], 1)[0]
    raw = ocd.read_bytes(symbols["bench_trace"], TRACE_CAPACITY * EVENT_SIZE)
    first = max(0, head - TRACE_CAPACITY)
    events = []
    for index in range(first, head):
        off = (index % TRACE_CAPACITY) * EVENT_SIZE
        t_ms, kind, length = struct.unpack_from("<IBB", raw, off)
        data = raw[off + 6 : off + 6 + min(length, 10)]
        events.append((index, t_ms, kind, list(data)))
    return head, events


def print_events(events, origin):
    for index, t_ms, kind, data in events:
        rel = (t_ms - origin) & 0xFFFFFFFF
        print(f"{index:6d} {rel / 1000:10.3f}  {describe_event(kind, data)}")


def cmd_status(ocd, symbols, args):
    status = decode_status(read_status(ocd, symbols))
    for key, value in status.items():
        if key == "diagnostics":
            print("diagnostics:")
            for name, count in value.items():
                print(f"  {name:24s} {count}")
        else:
            print(f"{key:14s} {value}")


def cmd_keys(ocd, symbols, args):
    ocd.write_byte(symbols["bench_virtual_keys"], int(args.mask, 0))


def cmd_tap(ocd, symbols, args):
    addr = symbols["bench_virtual_keys"]
    current = ocd.read_words(addr & ~3, 1)[0] >> ((addr & 3) * 8) & 0xFF
    ocd.write_byte(addr, current | (1 << args.key))
    time.sleep(args.hold_ms / 1000.0)
    ocd.write_byte(addr, current & ~(1 << args.key))


def cmd_mark(ocd, symbols, args):
    ocd.write_word(symbols["bench_mark"], args.value)


def cmd_raw(ocd, symbols, args):
    data = bytes.fromhex("".join(args.hexbytes))
    if not 1 <= len(data) <= 15:
        sys.exit("raw frame must be 1..15 bytes")
    base = symbols["bench_raw_tx"]
    for i, b in enumerate(data):
        ocd.write_byte(base + 1 + i, b)
    ocd.write_byte(base, len(data))  # the length last, so a partial frame never goes out


DIAG_ABORT = {0: "none", 1: "RF", 2: "TX", 3: "RX", 4: "OPENBOOT", 5: "WINDOW", 6: "LATE", 7: "RXLINE"}


def cmd_diag(ocd, symbols, args):
    """Wake the module with a preamble, request the diag dump and decode it."""
    base = symbols["bench_raw_tx"]
    head_before = ocd.read_words(symbols["bench_trace_head"], 1)[0]
    ocd.write_byte(base + 1, 0x00)
    ocd.write_byte(base, 1)
    time.sleep(0.01)
    for i, b in enumerate((0xA6, 0x71, 0x17)):
        ocd.write_byte(base + 1 + i, b)
    ocd.write_byte(base, 3)
    time.sleep(0.5)
    _, events = read_trace(ocd, symbols)
    rx = bytearray()
    for index, _, kind, data in events:
        if index >= head_before and kind == EV_RX:
            rx += bytes(data)
    start = rx.find(b"\x5d")
    if start < 0:
        print("no reply; the module did not answer A6 71")
        return
    frame = bytes(rx[start:])
    if len(frame) >= 2 and frame[1] == 0:
        print("dump refused: the module is connected (it only answers while idle or searching)")
        return
    if len(frame) < 3 + 85:
        print(f"short dump ({len(frame)} bytes): {frame.hex(' ')}")
        return
    names = ["rf_state", "ll_boot", "pair_bcast", "valid_rx", "entered_connected", "rf_config", "pair_rx_off", "wfi", "sleep_attempt", "sleep_entered", "sleep_aborted", "wake_gpio", "wake_rtc", "last_abort", "cfg_status", "rx_status", "tx_status", "ll_drop", "boot_reset", "fault_marker", "mepc", "mcause", "mtval", "loop_passes", "loop_stage", "ll_hid_rx", "ll_hid_tx", "ll_hid_rx_down", "ll_hid_tx_down", "ll_hid_tx_done_down"]
    values = struct.unpack_from("<B7I5HBBBBIBBIIIHB5I", frame, 3)
    for name, value in zip(names, values):
        if name == "last_abort":
            value = f"{value} ({DIAG_ABORT.get(value, '?')})"
        elif name in ("mepc", "mcause", "mtval", "ll_boot"):
            value = f"0x{value:08X}"
        print(f"{name:20s} {value}")


def cmd_trace(ocd, symbols, args):
    seen = args.since
    origin = None
    while True:
        head, events = read_trace(ocd, symbols)
        fresh = [e for e in events if e[0] >= seen]
        if fresh:
            if origin is None:
                origin = fresh[0][1] if args.relative else 0
            print_events(fresh, origin)
            seen = fresh[-1][0] + 1
            sys.stdout.flush()
        if not args.follow:
            break
        time.sleep(0.2)


def cmd_flash(ocd, symbols, args):
    print(ocd.cmd(f"program {args.elf} verify reset", timeout=120.0))


def cmd_reset(ocd, symbols, args):
    print(ocd.cmd("reset run"))


def cmd_stop(ocd, symbols, args):
    try:
        with open(PID_FILE) as f:
            pid = int(f.read())
    except (OSError, ValueError):
        print("no openocd started by this tool")
        return
    try:
        command = subprocess.check_output(["ps", "-p", str(pid), "-o", "comm="], text=True).strip()
    except subprocess.CalledProcessError:
        command = ""
    if os.path.basename(command) != "openocd":
        os.unlink(PID_FILE)
        print(f"pid {pid} is not openocd any more; nothing stopped")
        return
    os.kill(pid, 15)
    os.unlink(PID_FILE)
    print(f"stopped openocd pid {pid}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--elf", default=DEFAULT_ELF, help="bench firmware ELF for symbol addresses")
    parser.add_argument("--cfg", default=DEFAULT_CFG, help="OpenOCD configuration")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status").set_defaults(func=cmd_status)
    p = sub.add_parser("keys")
    p.add_argument("mask")
    p.set_defaults(func=cmd_keys)
    p = sub.add_parser("tap")
    p.add_argument("key", type=int)
    p.add_argument("hold_ms", type=int, nargs="?", default=60)
    p.set_defaults(func=cmd_tap)
    p = sub.add_parser("mark")
    p.add_argument("value", type=int)
    p.set_defaults(func=cmd_mark)
    p = sub.add_parser("raw", help="send bytes straight to the UART, bypassing the driver")
    p.add_argument("hexbytes", nargs="+")
    p.set_defaults(func=cmd_raw)
    sub.add_parser("diag", help="preamble, then A6 71, then decode the module's counter dump").set_defaults(func=cmd_diag)
    p = sub.add_parser("trace")
    p.add_argument("--follow", action="store_true")
    p.add_argument("--since", type=int, default=0, help="first event index to print")
    p.add_argument("--relative", action="store_true", help="timestamps relative to the first printed event")
    p.set_defaults(func=cmd_trace)
    p = sub.add_parser("flash")
    p.add_argument("elf", nargs="?", default=DEFAULT_ELF)
    p.set_defaults(func=cmd_flash)
    sub.add_parser("reset").set_defaults(func=cmd_reset)
    sub.add_parser("stop").set_defaults(func=cmd_stop)
    args = parser.parse_args()

    if args.command == "stop":
        cmd_stop(None, None, args)
        return

    symbols = load_symbols(args.elf) if args.command != "flash" else {}
    ocd = OpenOCD(args.cfg)
    ocd.connect()
    try:
        args.func(ocd, symbols, args)
    finally:
        ocd.sock.close()


if __name__ == "__main__":
    main()
