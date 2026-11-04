#!/usr/bin/env python3
"""Log the Plane Radar's serial output and track heap over a long run.

Writes, in --out (default: heap-logs/):
  serial-<stamp>.log   every line, prefixed with local time
  heap-<stamp>.csv     one row per "heap:" line (logged once a minute)
and prints a summary when it ends (after --hours, or Ctrl-C / SIGTERM).

Opening the USB serial port can reboot the board once at the start.

Usage:
  ~/.platformio/penv/bin/python scripts/heap_monitor.py --hours 4
  nohup ~/.platformio/penv/bin/python scripts/heap_monitor.py --hours 6 \\
      > heap-logs/monitor.out 2>&1 &
  ~/.platformio/penv/bin/python scripts/heap_monitor.py --replay some.log
"""

import argparse
import csv
import datetime as dt
import os
import re
import signal
import sys
import time

HEAP_RE = re.compile(
    r"heap: free (\d+) min (\d+) largest (\d+) \(lowest (\d+)\) "
    r"stack-free (\d+) up (\d+)d (\d+):(\d+)"
)
HEAP_LOW_RE = re.compile(r"heap low: (\d+) after (.+)")
BOOT_RE = re.compile(r"^Plane Radar .*\(built ")
RESET_REASON_RE = re.compile(r"Reset reason: (\d+)")
TROUBLE_RE = re.compile(
    r"Guru Meditation|assert failed|abort\(\)|panic|Backtrace:|"
    r"Stack canary|LoadProhibited|StoreProhibited|Self-recovery restart|"
    r"WiFi lost",
    re.IGNORECASE,
)
# Matches config::kMinLargestFreeBlockBytes: below this the firmware restarts.
RESTART_THRESHOLD = 20 * 1024
ESPRESSIF_VID = 0x303A


class Stats:
    def __init__(self):
        self.samples = 0
        self.min_free = None
        self.min_largest = None
        self.min_largest_at = ""
        self.min_ever = None
        self.below_threshold = 0
        self.boots = 0
        self.reset_reasons = []
        self.trouble = []
        self.lowest_steps = {}
        self.first_free = None
        self.last_free = None
        self.max_uptime_min = 0

    def heap(self, stamp, free, min_ever, largest, uptime_min):
        self.samples += 1
        if self.first_free is None:
            self.first_free = free
        self.last_free = free
        self.min_free = free if self.min_free is None else min(self.min_free, free)
        if self.min_largest is None or largest < self.min_largest:
            self.min_largest = largest
            self.min_largest_at = stamp
        self.min_ever = min_ever if self.min_ever is None else min(self.min_ever, min_ever)
        if largest < RESTART_THRESHOLD:
            self.below_threshold += 1
        self.max_uptime_min = max(self.max_uptime_min, uptime_min)

    def summary(self, started, ended):
        dur = ended - started
        lines = [
            "",
            "==== Heap monitor summary ====",
            f"Ran:              {started:%Y-%m-%d %H:%M} -> {ended:%H:%M} ({dur})",
            f"heap: samples:    {self.samples}",
        ]
        if self.samples:
            lines += [
                f"Free heap:        first {self.first_free}  last {self.last_free}  "
                f"lowest {self.min_free}",
                f"Lowest-ever free: {self.min_ever} (firmware's own min since boot)",
                f"Largest block:    lowest {self.min_largest} at {self.min_largest_at} "
                f"(restart threshold {RESTART_THRESHOLD})",
                f"Samples below threshold: {self.below_threshold}",
                f"Longest uptime seen: {self.max_uptime_min // 60}h "
                f"{self.max_uptime_min % 60:02d}m",
            ]
            if self.first_free and self.last_free and self.samples > 10:
                drift = self.last_free - self.first_free
                lines.append(f"Free heap drift first->last: {drift:+d} bytes")
        lines.append(f"Boots seen:       {self.boots} "
                     f"(reset reasons: {self.reset_reasons or 'none'})")
        if self.lowest_steps:
            worst = sorted(self.lowest_steps.items(), key=lambda kv: kv[1])[:5]
            lines.append("Lowest 'heap low' by step: " +
                         ", ".join(f"{k} {v}" for k, v in worst))
        if self.trouble:
            lines.append(f"Trouble lines ({len(self.trouble)}):")
            lines += [f"  {t}" for t in self.trouble[-10:]]
        else:
            lines.append("Trouble lines:    none")
        return "\n".join(lines)


def find_port():
    from serial.tools import list_ports

    for p in list_ports.comports():
        if p.vid == ESPRESSIF_VID:
            return p.device
    return None


def open_port(port):
    import serial

    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.5
    # Keep DTR/RTS low so opening is less likely to reset the chip.
    s.dtr = False
    s.rts = False
    s.open()
    return s


def serial_lines(port, deadline, stop):
    """Yield decoded lines; reopens the port if the board re-enumerates."""
    s = None
    buf = b""
    while not stop() and time.time() < deadline:
        if s is None:
            try:
                s = open_port(port)
                print(f"[monitor] opened {port}", flush=True)
            except Exception as e:  # port gone while the board reboots
                print(f"[monitor] waiting for {port}: {e}", flush=True)
                time.sleep(2)
                continue
        try:
            chunk = s.read(4096)
        except Exception as e:
            print(f"[monitor] port dropped ({e}); reopening", flush=True)
            try:
                s.close()
            except Exception:
                pass
            s = None
            time.sleep(1)
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            yield raw.decode("utf-8", "replace").rstrip("\r")
    if s is not None:
        s.close()


def replay_lines(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            yield line.rstrip("\r\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: first Espressif USB device)")
    ap.add_argument("--hours", type=float, default=4.0, help="how long to run (default 4)")
    ap.add_argument("--out", default="heap-logs", help="output directory")
    ap.add_argument("--replay", help="parse an existing log file instead of the port")
    ap.add_argument("--quiet", action="store_true", help="only print heap/trouble lines")
    args = ap.parse_args()

    stopping = {"flag": False}

    def on_signal(signum, frame):
        stopping["flag"] = True

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    os.makedirs(args.out, exist_ok=True)
    started = dt.datetime.now()
    stamp = started.strftime("%Y%m%d-%H%M%S")
    log_path = os.path.join(args.out, f"serial-{stamp}.log")
    csv_path = os.path.join(args.out, f"heap-{stamp}.csv")

    if args.replay:
        lines = replay_lines(args.replay)
    else:
        port = args.port or find_port()
        if not port:
            sys.exit("No Espressif USB serial device found; pass --port.")
        deadline = time.time() + args.hours * 3600
        lines = serial_lines(port, deadline, lambda: stopping["flag"])
        print(f"[monitor] logging {args.hours} h to {log_path} and {csv_path}", flush=True)

    stats = Stats()
    with open(log_path, "w", encoding="utf-8") as log, \
            open(csv_path, "w", newline="", encoding="utf-8") as cf:
        w = csv.writer(cf)
        w.writerow(["time", "free", "min_ever", "largest", "lowest_largest",
                    "stack_free", "uptime_min", "below_threshold"])
        for line in lines:
            now = dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            log.write(f"{now} {line}\n")
            log.flush()
            show = not args.quiet

            m = HEAP_RE.search(line)
            if m:
                free, min_ever, largest, lowest, stack = map(int, m.groups()[:5])
                d, h, mi = map(int, m.groups()[5:])
                up = d * 1440 + h * 60 + mi
                below = largest < RESTART_THRESHOLD
                w.writerow([now, free, min_ever, largest, lowest, stack, up, int(below)])
                cf.flush()
                stats.heap(now, free, min_ever, largest, up)
                show = True
                if below:
                    line += "   <-- below restart threshold"
            m = HEAP_LOW_RE.search(line)
            if m:
                val, step = int(m.group(1)), m.group(2).strip()
                stats.lowest_steps[step] = min(val, stats.lowest_steps.get(step, val))
            if BOOT_RE.search(line):
                stats.boots += 1
                show = True
            m = RESET_REASON_RE.search(line)
            if m:
                stats.reset_reasons.append(int(m.group(1)))
            if TROUBLE_RE.search(line):
                stats.trouble.append(f"{now} {line}")
                show = True
            if show:
                print(f"{now} {line}", flush=True)
            if stopping["flag"]:
                break

    text = stats.summary(started, dt.datetime.now())
    print(text, flush=True)
    with open(os.path.join(args.out, f"summary-{stamp}.txt"), "w", encoding="utf-8") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    main()
