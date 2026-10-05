#!/usr/bin/env python3
"""Inventory a decrypted PSP POPS PRX using ELF metadata and Capstone.

Independent discovery is kept separate from optional comparison against an old
text disassembly. The reference map is validation input, not a requirement for
analysis.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import struct
from collections import defaultdict
from pathlib import Path

from capstone import Cs, CS_ARCH_MIPS, CS_MODE_LITTLE_ENDIAN, CS_MODE_MIPS32
from capstone.mips import MIPS_INS_J, MIPS_INS_JAL, MIPS_OP_IMM
from elftools.elf.elffile import ELFFile


REF_RE = re.compile(r"^; Subroutine (.+?) - Address 0x([0-9A-Fa-f]+)")


class Image:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        with path.open("rb") as fp:
            elf = ELFFile(fp)
            self.entry = int(elf.header.e_entry)
            self.etype = str(elf.header.e_type)
            self.machine = str(elf.header.e_machine)
            self.segments = []
            for seg in elf.iter_segments():
                h = seg.header
                self.segments.append(
                    {
                        "type": str(h.p_type),
                        "offset": int(h.p_offset),
                        "vaddr": int(h.p_vaddr),
                        "paddr": int(h.p_paddr),
                        "filesz": int(h.p_filesz),
                        "memsz": int(h.p_memsz),
                        "flags": int(h.p_flags),
                        "align": int(h.p_align),
                    }
                )

        self.loads = [s for s in self.segments if s["type"] == "PT_LOAD"]
        if not self.loads:
            raise ValueError("ELF has no PT_LOAD")

    def vaddr_to_offset(self, addr: int) -> int:
        for seg in self.loads:
            if seg["vaddr"] <= addr < seg["vaddr"] + seg["filesz"]:
                return seg["offset"] + (addr - seg["vaddr"])
        raise ValueError(f"virtual address 0x{addr:x} is not file-backed")

    def bytes_at(self, addr: int, size: int) -> bytes:
        if size < 0:
            raise ValueError("negative read size")
        for seg in self.loads:
            if seg["vaddr"] <= addr < seg["vaddr"] + seg["filesz"]:
                if addr + size > seg["vaddr"] + seg["filesz"]:
                    raise ValueError("read crosses a file-backed segment boundary")
                off = seg["offset"] + addr - seg["vaddr"]
                result = self.data[off : off + size]
                if len(result) != size:
                    raise ValueError("truncated ELF segment")
                return result
        raise ValueError(f"virtual address 0x{addr:x} is not file-backed")

    def cstr(self, addr: int, limit: int = 256) -> str:
        off = self.vaddr_to_offset(addr)
        end = self.data.find(b"\0", off, min(len(self.data), off + limit))
        if end < 0:
            end = min(len(self.data), off + limit)
        return self.data[off:end].decode("ascii", "replace")


def parse_module_info(img: Image) -> dict:
    # PSP PRX files without a section table store the module-info file offset
    # in p_paddr of the first PT_LOAD.
    # Kernel PRXs set bit 31 as a flag; it is not part of the file offset.
    off = img.loads[0]["paddr"] & 0x7FFFFFFF
    if off <= 0 or off + 52 > len(img.data):
        raise ValueError(f"bad PSP module-info file offset 0x{off:x}")

    attr, ver_lo, ver_hi = struct.unpack_from("<HBB", img.data, off)
    name = img.data[off + 4 : off + 32].split(b"\0", 1)[0].decode("ascii", "replace")
    gp, ent_top, ent_end, stub_top, stub_end = struct.unpack_from("<IIIII", img.data, off + 32)

    first = img.loads[0]
    vaddr = first["vaddr"] + (off - first["offset"])
    return {
        "file_offset": off,
        "vaddr": vaddr,
        "attribute": attr,
        "version": [ver_hi, ver_lo],
        "name": name,
        "gp": gp,
        "ent_top": ent_top,
        "ent_end": ent_end,
        "stub_top": stub_top,
        "stub_end": stub_end,
    }


def parse_exports(img: Image, mod: dict) -> list[dict]:
    """Read export descriptors before relocation, preserving functions vs data."""
    cursor, end = mod["ent_top"], mod["ent_end"]
    if cursor > end:
        raise ValueError("reversed export descriptor range")
    result = []
    while cursor < end:
        if end - cursor < 16:
            raise ValueError("truncated export descriptor")
        name, flags, counts, table = struct.unpack("<4I", img.bytes_at(cursor, 16))
        words = counts & 0xFF
        variables = (counts >> 8) & 0xFF
        functions = counts >> 16
        if words < 4 or cursor + words * 4 > end:
            raise ValueError(f"invalid export descriptor size at 0x{cursor:x}")
        count = functions + variables
        entries = []
        if count:
            raw = img.bytes_at(table, count * 8)
            for i in range(count):
                entries.append({
                    "nid": struct.unpack_from("<I", raw, i * 4)[0],
                    "address": struct.unpack_from("<I", raw, (count + i) * 4)[0],
                    "kind": "function" if i < functions else "variable",
                })
        result.append({"name": img.cstr(name) if name else "syslib",
                       "descriptor": cursor, "flags": flags,
                       "functions": functions, "variables": variables,
                       "entries": entries})
        cursor += words * 4
    return result


def parse_imports(img: Image, mod: dict) -> list[dict]:
    out = []
    cursor = mod["stub_top"]
    end = mod["stub_end"]
    index = 0

    while cursor < end:
        # Five words are the mandatory descriptor; a sixth is optional.
        # Requiring 24 bytes silently drops the final 20-byte descriptor.
        if end - cursor < 20:
            raise ValueError("truncated import descriptor")
        raw = img.bytes_at(cursor, 20)
        name_ptr, flags, counts, nids, funcs = struct.unpack("<5I", raw)
        entsize_words = counts & 0xFF
        var_count = (counts >> 8) & 0xFF
        func_count = (counts >> 16) & 0xFFFF
        if entsize_words < 5 or cursor + entsize_words * 4 > end:
            raise ValueError(f"invalid import descriptor size at 0x{cursor:x}")
        variables = (struct.unpack("<I", img.bytes_at(cursor + 20, 4))[0]
                     if entsize_words >= 6 else None)
        if var_count and variables is None:
            raise ValueError("variable imports have no variable table")

        try:
            libname = img.cstr(name_ptr)
        except ValueError:
            libname = f"<bad-name@0x{name_ptr:08x}>"

        entries = []
        for i in range(func_count):
            try:
                nid = struct.unpack("<I", img.bytes_at(nids + i * 4, 4))[0]
            except ValueError:
                nid = None
            entries.append({"index": i, "nid": nid, "stub": funcs + i * 8})

        out.append(
            {
                "index": index,
                "descriptor": cursor,
                "name": libname,
                "flags": flags,
                "func_count": func_count,
                "var_count": var_count,
                "nid_table": nids,
                "func_table": funcs,
                "var_table": variables,
                "functions": entries,
            }
        )
        cursor += entsize_words * 4
        index += 1

    return out


def parse_reference(path: Path | None) -> dict[int, str]:
    if path is None:
        return {}
    result = {}
    with path.open("r", encoding="utf-8", errors="replace") as fp:
        for line in fp:
            match = REF_RE.match(line)
            if match:
                result[int(match.group(2), 16)] = match.group(1)
    return result


def discover_functions(
    img: Image, code_start: int, code_end: int
) -> tuple[dict[int, set[str]], dict]:
    md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN)
    md.detail = True

    blob = img.bytes_at(code_start, code_end - code_start)
    candidates: dict[int, set[str]] = defaultdict(set)
    candidates[img.entry].add("elf-entry")

    decoded = 0
    direct_calls = 0
    direct_jumps = 0
    invalid_words = 0

    # MIPS/Allegrex instructions are fixed-width. Disassembling one word at a
    # time means unsupported VFPU opcodes cannot terminate the full scan.
    for off in range(0, len(blob) - 3, 4):
        pc = code_start + off
        insns = list(md.disasm(blob[off : off + 4], pc, count=1))
        if not insns:
            invalid_words += 1
            continue

        insn = insns[0]
        decoded += 1
        if insn.id not in (MIPS_INS_JAL, MIPS_INS_J) or not insn.operands:
            continue

        operand = insn.operands[0]
        if operand.type != MIPS_OP_IMM:
            continue

        target = int(operand.imm) & 0xFFFFFFFF
        if code_start <= target < code_end and target % 4 == 0:
            if insn.id == MIPS_INS_JAL:
                candidates[target].add("jal-target")
                direct_calls += 1
            else:
                direct_jumps += 1

    # Conservative prologue heuristic: stack allocation followed shortly by a
    # save of $ra. It recovers functions not reached through a direct JAL.
    for off in range(0, len(blob) - 24, 4):
        pc = code_start + off
        word = struct.unpack_from("<I", blob, off)[0]
        if (word & 0xFFFF0000) != 0x27BD0000 or (word & 0x8000) == 0:
            continue
        for look in range(4, 24, 4):
            w = struct.unpack_from("<I", blob, off + look)[0]
            if (w & 0xFFFF0000) == 0xAFBF0000:
                candidates[pc].add("stack-prologue")
                break

    return candidates, {
        "decoded_words": decoded,
        "invalid_words": invalid_words,
        "direct_calls": direct_calls,
        "direct_jumps": direct_jumps,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--reference-asm", type=Path)
    parser.add_argument("--out", type=Path, default=Path("out"))
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)

    img = Image(args.elf)
    mod = parse_module_info(img)
    imports = parse_imports(img, mod)

    # Target-specific scanning window, NOT a recovered .text section or proof
    # that all these bytes are executed. This PRX has no section headers.
    code_start = 0
    code_end = mod["ent_top"]
    candidates, disasm_stats = discover_functions(img, code_start, code_end)
    reference = parse_reference(args.reference_asm)

    import_stubs = {}
    for lib in imports:
        for fn in lib["functions"]:
            import_stubs[fn["stub"]] = {"library": lib["name"], "nid": fn["nid"]}
            candidates[fn["stub"]].add("import-stub")

    rows = []
    all_addrs = sorted(set(candidates) | set(reference))
    for addr in all_addrs:
        sources = sorted(candidates.get(addr, set()))
        rows.append(
            {
                "address": addr,
                "address_hex": f"0x{addr:08X}",
                "sources": sources,
                "reference_name": reference.get(addr),
                "reference_match": addr in reference and bool(sources),
                "import": import_stubs.get(addr),
            }
        )

    detected = set(candidates)
    ref_addrs = set(reference)
    report = {
        "input": {"path": str(args.elf), "size": args.elf.stat().st_size,
                  "sha256": hashlib.sha256(img.data).hexdigest()},
        "elf": {
            "entry": img.entry,
            "type": img.etype,
            "machine": img.machine,
            "segments": img.segments,
        },
        "module": mod,
        "code_range": {"start": code_start, "end": code_end, "size": code_end - code_start,
                       "classification": "candidate_scan_window_not_proven_code"},
        "imports": imports,
        "disassembly": disasm_stats,
        "functions": {
            "candidate_count": len(detected),
            "reference_count": len(ref_addrs),
            "overlap_count": len(detected & ref_addrs),
            "reference_recall": (
                len(detected & ref_addrs) / len(ref_addrs) if ref_addrs else None
            ),
            "rows": rows,
        },
    }

    (args.out / "analysis.json").write_text(
        json.dumps(report, indent=2), encoding="utf-8"
    )

    with (args.out / "functions.csv").open("w", newline="", encoding="utf-8") as fp:
        writer = csv.writer(fp)
        writer.writerow(
            [
                "address",
                "sources",
                "reference_name",
                "reference_match",
                "import_library",
                "nid",
            ]
        )
        for row in rows:
            imp = row["import"] or {}
            nid = imp.get("nid")
            writer.writerow(
                [
                    row["address_hex"],
                    "|".join(row["sources"]),
                    row["reference_name"] or "",
                    int(row["reference_match"]),
                    imp.get("library", ""),
                    f"0x{nid:08X}" if nid is not None else "",
                ]
            )

    print(f"module: {mod['name']} entry=0x{img.entry:08X}")
    print(
        f"code window: 0x{code_start:08X}..0x{code_end:08X} "
        f"({code_end - code_start:,} bytes)"
    )
    print(
        f"imports: {sum(x['func_count'] for x in imports)} funcs "
        f"across {len(imports)} libraries"
    )
    print(f"Capstone candidates: {len(detected)}")
    if reference:
        overlap = len(detected & ref_addrs)
        print(
            f"reference: {len(ref_addrs)} functions; overlap {overlap} "
            f"({100.0 * overlap / len(ref_addrs):.1f}%)"
        )
        print(
            "reference functions not independently seeded: "
            f"{len(ref_addrs - detected)}"
        )

    print(f"wrote {args.out / 'analysis.json'} and {args.out / 'functions.csv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
