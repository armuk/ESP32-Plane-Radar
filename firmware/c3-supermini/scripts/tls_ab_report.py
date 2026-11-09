#!/usr/bin/env python3
"""Summarise "tls-ab:" lines from the supermini_tls_ab measurement build.

Each line is one HTTPS request; the firmware alternates certificate checking
on and off per host. This compares the two per host: success rate, time, and
the request's heap low point.

Usage:
  ~/.platformio/penv/bin/python scripts/tls_ab_report.py heap-logs/serial-*.log
"""

import re
import statistics
import sys
from collections import defaultdict

LINE_RE = re.compile(
    r"tls-ab: (\S+) verify=(\d) code=(-?\d+) ms=(\d+) free_before=(\d+) "
    r"largest_before=(\d+) local_min=(\d+) drop=(\d+)(?: err=(.*))?"
)


def main(paths):
    rows = defaultdict(list)
    errors = defaultdict(lambda: defaultdict(int))
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = LINE_RE.search(line)
                if not m:
                    continue
                tag, verify = m.group(1), int(m.group(2))
                code, ms = int(m.group(3)), int(m.group(4))
                local_min, drop = int(m.group(7)), int(m.group(8))
                rows[(tag, verify)].append((code, ms, local_min, drop))
                if m.group(9):
                    errors[(tag, verify)][m.group(9).strip()] += 1
    if not rows:
        sys.exit("No tls-ab lines found. Is the supermini_tls_ab build running?")

    print(f"{'host':10} {'check':5} {'n':>4} {'ok%':>5} {'ms med':>7} "
          f"{'drop med':>8} {'drop max':>8} {'low med':>7} {'low min':>7}")
    tags = sorted({t for t, _ in rows})
    for tag in tags:
        for verify in (0, 1):
            r = rows.get((tag, verify), [])
            if not r:
                continue
            ok = [x for x in r if x[0] == 200 or x[0] == 404]
            drops = [x[3] for x in ok] or [0]
            lows = [x[2] for x in ok] or [0]
            mss = [x[1] for x in ok] or [0]
            print(f"{tag:10} {'yes' if verify else 'no':5} {len(r):4d} "
                  f"{100 * len(ok) / len(r):5.0f} {statistics.median(mss):7.0f} "
                  f"{statistics.median(drops):8.0f} {max(drops):8d} "
                  f"{statistics.median(lows):7.0f} {min(lows):7d}")
        a, b = rows.get((tag, 0), []), rows.get((tag, 1), [])
        a_ok = [x[3] for x in a if x[0] in (200, 404)]
        b_ok = [x[3] for x in b if x[0] in (200, 404)]
        if a_ok and b_ok:
            print(f"{'':10} extra heap with checking: median "
                  f"{statistics.median(b_ok) - statistics.median(a_ok):+.0f}, "
                  f"worst {max(b_ok) - max(a_ok):+d} bytes")
    for key, errs in errors.items():
        tag, verify = key
        for text, n in errs.items():
            print(f"error {tag} check={'yes' if verify else 'no'} x{n}: {text}")
    print("\nok = HTTP 200/404. drop = free heap before the request minus its "
          "low point. low = the request's lowest free heap.")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
