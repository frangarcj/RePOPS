#!/usr/bin/env python3
"""Extract useful function names from an ARK-style PSP disassembly listing."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path

MARKER = re.compile(r"^; Subroutine (.+?) - Address 0x([0-9A-Fa-f]+)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("listing", type=Path)
    ap.add_argument("output", type=Path)
    args = ap.parse_args()

    rows = []
    with args.listing.open("r", encoding="utf-8", errors="replace") as fp:
        for line in fp:
            match = MARKER.match(line)
            if not match:
                continue
            name = match.group(1)
            if name.startswith("sub_") or name.startswith("loc_"):
                continue
            rows.append((int(match.group(2), 16), name))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as fp:
        writer = csv.writer(fp)
        writer.writerow(["address", "name"])
        for address, name in sorted(rows):
            writer.writerow([f"0x{address:08X}", name])

    print(f"wrote {len(rows)} useful symbols to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
