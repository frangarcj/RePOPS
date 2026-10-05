#!/usr/bin/env python3
"""Join actual PRX imports/exports with Ghidra's static caller inventory.

Firmware inputs and bulk pseudocode are never copied into the report. Library
names are matched together with NIDs, not confused with provider module names.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path

from analyze_pops import Image, parse_exports, parse_imports, parse_module_info

POPS_SHA256 = "6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0"
POPSMAN_SHA256 = "83ed5373388ba2af57f26eba421eacdee66f56bd3e72432af38365ae8bf4d855"

# These are investigation labels, not recovered Sony function names.
HINTS = {
    0x8D5A07D2: ("file_io", "Opens a file and queries its UID control block."),
    0x528266FA: ("file_io_and_me", "File reads and a path to DE630CD2; not solely audio."),
    0x14447BA0: ("content_mac", "Calls BBMac init/update/final services."),
    0x42F0EA37: ("file_io", "Open/ioctl/read/close path."),
    0xAE5AC375: ("unresolved", "Two internal helpers; role not established."),
    0xF6637A72: ("content_mac", "Calls BBMac init/update/final services."),
    0x83378E12: ("kernel_stack_wrapper", "Calls sceKernelExtendKernelStack."),
    0xE907AE69: ("unresolved", "Calls internal helper 0xD3C."),
    0x9B4AAF7D: ("string_and_internal_state", "Uses strnlen/strncpy and internal helper."),
    0xBD5F7689: ("file_io", "Open/ioctl/read/close path with internal preparation."),
    0x2AB4FE43: ("file_io", "Read/read-async/wait-async; large control-flow routine."),
    0x0FA28FE6: ("file_io", "Calls sceIoLseek."),
    0x805D1205: ("file_io", "Calls sceIoClose."),
    0xA6EDDF16: ("system_ui", "Calls impose and RTC services."),
    0x4F5B6D82: ("system_ui", "Calls an impose service."),
    0xD4F17F54: ("system_ui", "K1-preserving impose wrapper."),
    0x54F2AE52: ("system_ui", "K1-preserving impose wrapper."),
    0x1A23C094: ("system_ui_power", "Calls impose and power-tick services."),
    0x69C4BCCB: ("display", "Calls display and impose services."),
    0x2AC64C3F: ("graphics", "Sets GE EDRAM size to 0x400000 then queries it."),
    0x30BE34E4: ("file_io", "Seek followed by read only when seek result matches."),
    0x3771229C: ("file_io", "Seek/read wrapper; exact contract pending."),
    0x8A8DFE17: ("power", "K1-preserving sceKernelPowerUnlock wrapper."),
    0xDE630CD2: ("media_engine_start", "Validates two arguments, stores entry and patches stack immediates before 0x35D8."),
    0x68C55F4C: ("media_engine_handshake", "Writes 0xBFC007F8, polls 0xBFC007F0, calls codec service."),
    0xC93C56F8: ("media_engine_mailbox", "Writes unsigned argument >> 5 to 0xBFC007F4 then SYNC; volume role is a hypothesis."),
    0x0BABD960: ("media_engine_mailbox", "K1-based check then writes argument + 2 to 0xBFC007F8; payload role unresolved."),
    0x7014C540: ("graphics", "GE MMIO/cache/interrupt path with GE enqueue/list-sync fallback; not labelled SetPause."),
    0xE7F06E2B: ("graphics", "Writes argument & 0x1FFFFFFF to GE register 0xBD40010C."),
    0x0090B2C8: ("exit", "Exits through the loadexec service; does not return normally."),
}


def checked_index(path: Path, sha256: str) -> dict:
    index = json.loads(path.read_text())
    if index.get("input_sha256", "").lower() != sha256:
        raise ValueError(f"Ghidra input hash mismatch: {path}")
    if index.get("language") != "Allegrex:LE:32:default":
        raise ValueError(f"Expected Allegrex analysis: {path}")
    if int(index["image_base"], 16) != 0:
        raise ValueError("Only base-zero indexes supported; refusing wrong addresses")
    return index


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pops", type=Path, default=Path("build/pops_660.prx.dec"))
    ap.add_argument("--popsman", type=Path, default=Path("build/f0-kd-popsman.prx"))
    ap.add_argument("--pops-index", type=Path, default=Path("out/decompiled/pops_660/index.json"))
    ap.add_argument("--popsman-index", type=Path, default=Path("out/decompiled/popsman_660/index.json"))
    ap.add_argument("--out", type=Path, required=True, help="New report directory")
    args = ap.parse_args()
    if args.out.exists():
        raise ValueError(f"Refusing existing output: {args.out}")

    consumer, provider = Image(args.pops), Image(args.popsman)
    for img, expected in ((consumer, POPS_SHA256), (provider, POPSMAN_SHA256)):
        if hashlib.sha256(img.data).hexdigest() != expected:
            raise ValueError(f"Unexpected firmware input: {img.path}")
    ci = checked_index(args.pops_index, POPS_SHA256)
    pi = checked_index(args.popsman_index, POPSMAN_SHA256)
    exports = parse_exports(provider, parse_module_info(provider))
    export_map = {}
    for lib in exports:
        for fn in lib["entries"]:
            if fn["kind"] != "function":
                continue
            key = lib["name"], fn["nid"]
            if key in export_map:
                raise ValueError(f"Duplicate export key: {key}")
            export_map[key] = fn["address"]
    provider_functions = {int(x["address"], 16): x for x in pi["functions"]}
    consumer_functions = {int(x["address"], 16): x for x in ci["functions"]}
    rows = []
    for lib in parse_imports(consumer, parse_module_info(consumer)):
        for fn in lib["functions"]:
            key = lib["name"], fn["nid"]
            if key not in export_map:
                continue
            address = export_map[key]
            pf = provider_functions.get(address, {})
            cf = consumer_functions.get(fn["stub"], {})
            callers = []
            for f in ci["functions"]:
                if any(int(c["address"], 16) == fn["stub"] for c in f["known_callees"]):
                    callers.append({"address": f["address"], "name": f["name"],
                                    "decompiled": f["decompile_completed"]})
            family, note = HINTS.get(fn["nid"], ("unresolved", "Not classified"))
            rows.append({"library": lib["name"], "nid": f"0x{fn['nid']:08X}",
                         "consumer_stub": f"0x{fn['stub']:08X}",
                         "provider_entry": f"0x{address:08X}",
                         "symbol": cf.get("name", pf.get("name", "")),
                         "hypothesized_family": family, "evidence_note": note,
                         "provider_decompiled": pf.get("decompile_completed", False),
                         "provider_warnings": pf.get("has_warnings", None),
                         "provider_known_callees": pf.get("known_callees", []),
                         "consumer_static_callers": callers})
    report = {
        "consumer_sha256": POPS_SHA256, "provider_sha256": POPSMAN_SHA256,
        "provider_module": parse_module_info(provider)["name"],
        "provenance_warning": "ARK reference exports noAudio/noAudio_driver; not established as a pristine Sony image.",
        "method": "Raw ELF library+NID join; Ghidra static calls. No runtime coverage or ABI proof.",
        "matched_imports": len(rows), "functions": rows,
        "provider_export_libraries": [{"name": e["name"], "functions": e["functions"]} for e in exports],
    }
    args.out.mkdir(parents=True)
    (args.out / "contract.json").write_text(json.dumps(report, indent=2) + "\n")
    with (args.out / "contract.csv").open("w", newline="") as fp:
        writer = csv.writer(fp)
        writer.writerow(["library", "nid", "consumer_stub", "provider_entry", "family", "static_callers", "note"])
        for row in rows:
            writer.writerow([row["library"], row["nid"], row["consumer_stub"], row["provider_entry"],
                             row["hypothesized_family"], "|".join(c["address"] for c in row["consumer_static_callers"]), row["evidence_note"]])
    print(f"{len(rows)} actual import/export matches; wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
