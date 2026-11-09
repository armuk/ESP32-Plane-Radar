#!/usr/bin/env python3
"""Read the crash dump the firmware saved to flash and print a backtrace.

After a crash (panic, watchdog, failed assert) ESP-IDF writes a core dump to
the `coredump` partition. This reads it over USB and decodes it with
Espressif's esp_coredump and the RISC-V gdb that PlatformIO installs. The
decode needs the firmware.elf that was running when it crashed; the latest
build's is the default.

Reading flash puts the board in download mode and resets it afterwards.

Usage (from the project folder):
  ~/.platformio/penv/bin/python scripts/read_coredump.py
  ~/.platformio/penv/bin/python scripts/read_coredump.py --erase   # clear after reading
  ~/.platformio/penv/bin/python scripts/read_coredump.py --file saved.bin --elf old.elf
"""

import argparse
import csv
import datetime as dt
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO_HOME = os.path.expanduser("~/.platformio")
DEFAULT_ELF = os.path.join(ROOT, ".pio", "build", "supermini", "firmware.elf")
PARTITIONS = os.path.join(ROOT, "partitions", "plane_radar.csv")
GDB = os.path.join(PIO_HOME, "packages", "tool-riscv32-esp-elf-gdb", "bin",
                   "riscv32-esp-elf-gdb")
ESPRESSIF_VID = 0x303A


def coredump_partition():
    """(offset, size) of the coredump row in the partition table."""
    with open(PARTITIONS, encoding="utf-8") as f:
        rows = csv.reader(line for line in f if not line.lstrip().startswith("#"))
        for row in rows:
            cells = [c.strip() for c in row]
            if len(cells) >= 5 and cells[2] == "coredump":
                return int(cells[3], 0), int(cells[4], 0)
    sys.exit(f"No coredump partition in {PARTITIONS}")


def find_port():
    from serial.tools import list_ports

    for p in list_ports.comports():
        if p.vid == ESPRESSIF_VID:
            return p.device
    sys.exit("No Espressif USB serial device found; pass --port.")


def esptool(port, *args):
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32c3", "--port", port, *args]
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def is_empty(data):
    # An erased partition reads as 0xFF. IDF also leaves the header blank
    # after a dump has been erased, so check the first 64 bytes.
    head = data[:64]
    return len(head) == 0 or all(b == 0xFF for b in head)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: first Espressif USB device)")
    ap.add_argument("--elf", default=DEFAULT_ELF,
                    help="firmware.elf that crashed (default: latest build)")
    ap.add_argument("--file", help="decode a dump saved earlier instead of reading the board")
    ap.add_argument("--out", default="crash-dumps", help="where raw dumps are saved")
    ap.add_argument("--erase", action="store_true",
                    help="erase the coredump partition after a successful read")
    args = ap.parse_args()

    if not os.path.isfile(args.elf):
        sys.exit(f"ELF not found: {args.elf} (build first, or pass --elf)")
    if not os.path.isfile(GDB):
        sys.exit(f"gdb not found: {GDB} (PlatformIO installs it with the platform)")

    port = None
    if args.file:
        dump_path = args.file
    else:
        offset, size = coredump_partition()
        port = args.port or find_port()
        os.makedirs(args.out, exist_ok=True)
        stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        dump_path = os.path.join(args.out, f"coredump-{stamp}.bin")
        esptool(port, "read-flash", hex(offset), hex(size), dump_path)

    with open(dump_path, "rb") as f:
        data = f.read()
    if is_empty(data):
        print("No crash dump stored (partition is blank).")
        if not args.file:
            os.remove(dump_path)
        return

    print(f"\nCrash dump: {dump_path}\nELF:        {args.elf}\n", flush=True)
    decode = subprocess.run([
        sys.executable, "-m", "esp_coredump", "--chip", "esp32c3",
        "info_corefile", "--core", dump_path, "--core-format", "raw",
        "--gdb", GDB, args.elf,
    ])
    if decode.returncode != 0:
        print("\nDecode failed. If the firmware has changed since the crash, "
              "pass the matching --elf. The raw dump is kept.", file=sys.stderr)
        sys.exit(decode.returncode)

    if args.erase and port:
        offset, size = coredump_partition()
        esptool(port, "erase-region", hex(offset), hex(size))
        print("Coredump partition erased.")


if __name__ == "__main__":
    main()
