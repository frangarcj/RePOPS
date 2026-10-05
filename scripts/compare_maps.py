#!/usr/bin/env python3
"""Compare RePops Capstone/Ghidra function maps against an optional reference."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path

REF_RE = re.compile(r"^; Subroutine (.+?) - Address 0x([0-9A-Fa-f]+)")


def reference_map(path: Path) -> dict[int, str]:
    result = {}
    with path.open("r", encoding="utf-8", errors="replace") as fp:
        for line in fp:
            match = REF_RE.match(line)
            if match:
                result[int(match.group(2), 16)] = match.group(1)
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--capstone", type=Path, default=Path("out/analysis.json"))
    ap.add_argument("--ghidra", type=Path, default=Path("out/ghidra_functions.csv"))
    ap.add_argument("--reference", type=Path, required=True)
    ap.add_argument("--code-end", type=lambda x: int(x, 0), default=0x3D4A4)
    args = ap.parse_args()

    cap = json.loads(args.capstone.read_text(encoding="utf-8"))
    capset = {
        int(row["address"])
        for row in cap["functions"]["rows"]
        if int(row["address"]) < args.code_end and row["sources"]
    }

    with args.ghidra.open(newline="", encoding="utf-8") as fp:
        ghset = {
            int(row["entry"], 16)
            for row in csv.DictReader(fp)
            if int(row["entry"], 16) < args.code_end
        }

    ref = reference_map(args.reference)
    refset = {addr for addr in ref if addr < args.code_end}

    def pct(n: int, d: int) -> float:
        return 100.0 * n / d if d else 0.0

    print(f"reference: {len(refset)}")
    print(
        f"capstone:  {len(capset)}; overlap={len(capset & refset)} "
        f"({pct(len(capset & refset), len(refset)):.1f}% ref recall), "
        f"extras={len(capset - refset)}"
    )
    print(
        f"ghidra:    {len(ghset)}; overlap={len(ghset & refset)} "
        f"({pct(len(ghset & refset), len(refset)):.1f}% ref recall), "
        f"extras={len(ghset - refset)}"
    )
    print(f"capstone & ghidra: {len(capset & ghset)}")
    print(f"union:             {len(capset | ghset)}")

    missing = sorted(refset - ghset)
    extras = sorted(ghset - refset)
    if missing:
        print("ghidra missing reference entries:")
        for addr in missing[:40]:
            print(f"  0x{addr:08X} {ref[addr]}")
        if len(missing) > 40:
            print(f"  ... {len(missing) - 40} more")
    if extras:
        print("ghidra-only candidate entries:")
        for addr in extras[:40]:
            print(f"  0x{addr:08X}")
        if len(extras) > 40:
            print(f"  ... {len(extras) - 40} more")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
