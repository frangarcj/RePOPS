#!/usr/bin/env python3
"""Compare one ME drain/ACK iteration (+0x3164..+0x31C7) to native C.

Unchanged relocated corpus instructions are executed. Only the imported DDR
service is replaced by a test recorder. Device status/request values are
scripted; no mixer, PSP timing, caches or complete worker equivalence is claimed.
"""
from __future__ import annotations
import argparse
import ctypes as C
import hashlib
import itertools
import json
import random
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from analyze_pops import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA = "ff4222e8085190e1f357aece096282963e0e8e317b76547fafb1600a746a237e"
EXPORT_SHA = "92fc7ac3d92919f4c0f321804fc51a24714c8a22e25fafc26c40040d21efb7a5"
READ = C.CFUNCTYPE(C.c_uint32, C.c_void_p, C.c_uint32)
READ16 = C.CFUNCTYPE(C.c_uint16, C.c_void_p, C.c_uint32)
WRITE = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32, C.c_uint32)
SERVICE = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_uint32)
SAMPLE = C.CFUNCTYPE(C.c_bool, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32))

class Bus(C.Structure):
    _fields_ = [("context", C.c_void_p), ("provider", READ), ("read32", READ),
                ("read16", READ16), ("write32", WRITE), ("service", SERVICE), ("sample", SAMPLE)]

class Worker(C.Structure):
    _fields_ = [("phase", C.c_int), ("callback", C.c_uint32), ("packed", C.c_uint32),
                ("prefill", C.c_uint32), ("low", C.c_int32), ("high", C.c_int32)]


def checked_code(elf: Path, export: Path):
    image = Image(elf)
    if hashlib.sha256(image.data).hexdigest() != SOURCE_SHA:
        raise ValueError("Wrong corpus POPSMAN hash")
    manifest = json.loads((export / "manifest.json").read_text())
    expected = {"source_sha256": SOURCE_SHA, "language": "Allegrex:LE:32:default",
                "image_base": "00000000", "start": 0, "size": 0x3E60,
                "filename": "range.bin", "sha256": EXPORT_SHA,
                "kind": "initialized_memory_after_ghidra_relocation_not_original_file_bytes"}
    if any(manifest.get(k) != v for k, v in expected.items()):
        raise ValueError("Wrong Ghidra export metadata")
    code = (export / "range.bin").read_bytes()
    if len(code) != 0x3E60 or hashlib.sha256(code).hexdigest() != EXPORT_SHA:
        raise ValueError("Wrong relocated bytes")
    applied = {int(r["address"], 16) for r in manifest["relocations"] if r["status"] == "APPLIED"}
    original = image.bytes_at(0, len(code))
    if any(original[o:o+4] != code[o:o+4] and o not in applied for o in range(0, len(code), 4)):
        raise ValueError("Change outside an applied relocation")
    return code


def vectors(seed=660, count=256):
    edges = [-32768, -1, 0, 1, 32767]
    cases = list(itertools.product(edges, edges, [0, 2, 0x20, 0x22], [0, 1, 2, 3, 4, 0xFFFFFFFF]))
    rng = random.Random(seed)
    cases += [(rng.randrange(-32768, 32768), rng.randrange(-32768, 32768),
               rng.choice([0, 2, 0x20, 0x22]), rng.getrandbits(32)) for _ in range(count)]
    return cases


def exercise(code, native, cases):
    import unicorn as U
    import unicorn.mips_const as M
    uc = U.Uc(U.UC_ARCH_MIPS, U.UC_MODE_MIPS32 | U.UC_MODE_LITTLE_ENDIAN)
    uc.ctl_set_cpu_model(M.UC_CPU_MIPS32_24KF)
    uc.mem_map(0, 0x4000)
    uc.mem_write(0, code)
    # Synthetic recorder at the imported DDR stub, outside the tested interval.
    # LUI T0, marker; SW A0,0(T0); JR RA; NOP. It returns through the real JAL.
    uc.mem_write(0x3CFC, struct.pack("<4I", 0x3C081D00, 0xAD040000, 0x03E00008, 0))
    pages = (0x1E000000, 0x1FC00000, 0x1C200000)
    for page in (*pages, 0x1D000000):
        uc.mem_map(page, 4096)
    saved = uc.context_save()
    events, native_events, errors = [], [], []
    native_pages = {}
    ack_seen = False
    terminal = None

    def physical(address):
        return address & 0x1FFFFFFF if 0x80000000 <= address < 0xC0000000 else address

    def written(machine, access, address, size, value, userdata):
        nonlocal ack_seen
        address = physical(address)
        value &= 0xFFFFFFFF
        if size != 4:
            raise RuntimeError("Unexpected write width")
        if address == 0x1D000000:
            events.append(["service", 2, value])
            return
        if address not in (0x1E000070, 0x1FC007F0, 0x1C200000):
            raise RuntimeError(f"Unexpected write 0x{address:X}")
        events.append(["write", address, value])
        if address == 0x1FC007F0:
            ack_seen = True

    def instruction(machine, address, size, userdata):
        nonlocal terminal
        address = physical(address)
        if address in (0x302C, 0x31C8) or (address == 0x3164 and ack_seen):
            terminal = address
            machine.emu_stop()
        elif not (0x3164 <= address < 0x31C8 or 0x3CFC <= address < 0x3D0C):
            raise RuntimeError(f"Escaped drain interval: 0x{address:X}")

    uc.hook_add(U.UC_HOOK_MEM_WRITE, written)
    uc.hook_add(U.UC_HOOK_CODE, instruction)

    def read_native(ctx, address):
        if address not in (0xBE000028, 0xBFC007F8):
            errors.append(f"Unexpected native read {address:X}")
            return 0
        address = physical(address)
        return struct.unpack_from("<I", native_pages[address & ~4095], address & 4095)[0]

    def write_native(ctx, address, value):
        address = physical(address)
        if address not in (0x1E000070, 0x1FC007F0, 0x1C200000):
            errors.append(f"Unexpected native write {address:X}")
            return
        native_events.append(["write", address, value])
        struct.pack_into("<I", native_pages[address & ~4095], address & 4095, value)

    def unexpected(*args):
        errors.append("Unexpected callback in drain path")
        return 0

    def service_native(ctx, service, value):
        native_events.append(["service", service, value])

    callbacks = (READ(unexpected), READ(read_native), READ16(unexpected), WRITE(write_native),
                 SERVICE(service_native), SAMPLE(unexpected))
    bus = Bus(None, *callbacks)
    fn = native.rp_me_worker_step
    fn.argtypes = [C.POINTER(Worker), C.POINTER(Bus)]
    fn.restype = C.c_int
    pending = 0
    for number, (low, high, status, control) in enumerate(cases):
        uc.context_restore(saved)
        events.clear(); native_events.clear(); errors.clear()
        ack_seen = False; terminal = None
        native_pages = {page: bytearray(4096) for page in pages}
        struct.pack_into("<I", native_pages[0x1E000000], 0x28, status)
        struct.pack_into("<I", native_pages[0x1FC00000], 0x7F8, control)
        for page, content in native_pages.items():
            uc.mem_write(page, bytes(content))
        uc.reg_write(M.UC_MIPS_REG_S1, low & 0xFFFFFFFF)
        uc.reg_write(M.UC_MIPS_REG_S2, high & 0xFFFFFFFF)
        uc.reg_write(M.UC_MIPS_REG_K0, 0xBE000000)
        uc.reg_write(M.UC_MIPS_REG_K1, 0xBC200000)
        uc.emu_start(0x80003164, 0x80100000, count=160)
        worker = Worker(8, 0, 0, 0, low, high)  # RP_ME_DRAIN
        result = fn(C.byref(worker), C.byref(bus))
        waiting = (status & 0x22) == 0
        expected_phase = 8 if waiting or (control & 1) else (9 if control else 1)
        expected_terminal = None if waiting else (0x3164 if control & 1 else 0x31C8 if control else 0x302C)
        checks = {
            "effects": events == native_events,
            "memory": all(uc.mem_read(page, 4096) == data for page, data in native_pages.items()),
            "low": worker.low == C.c_int32(uc.reg_read(M.UC_MIPS_REG_S1)).value,
            "high": worker.high == C.c_int32(uc.reg_read(M.UC_MIPS_REG_S2)).value,
            "phase": worker.phase == expected_phase,
            "result": result == (1 if waiting else 4),
            "boundary": terminal == expected_terminal,
            "errors": not errors,
        }
        if not all(checks.values()):
            raise RuntimeError(json.dumps({"case": number, "input": [low, high, status, control],
                "checks": checks, "binary_events": events, "native_events": native_events,
                "errors": errors, "pc": hex(uc.reg_read(M.UC_MIPS_REG_PC))}))
        pending += waiting
    return {"cases_passed": len(cases), "pending_cases": pending,
            "scope": "drain/ACK writes, sample state, service argument and control-flow exit"}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--elf", type=Path, default=ROOT / "build/firmware_660/F0/kd/popsman.prx")
    ap.add_argument("--relocated", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--seed", type=int, default=660)
    args = ap.parse_args()
    if args.out.exists():
        ap.error("Refusing existing output directory")
    code = checked_code(args.elf, args.relocated)
    import unicorn as U
    if U.__version__ != "2.1.4":
        ap.error("This harness pins optional unicorn==2.1.4")
    args.out.mkdir(parents=True)
    source, header = ROOT / "src/native/me_worker.c", ROOT / "src/native/me_worker.h"
    report = {"status": "incomplete", "source_sha256": SOURCE_SHA,
        "export_sha256": EXPORT_SHA, "range": "0x3164..0x31C7",
        "range_sha256": hashlib.sha256(code[0x3164:0x31C8]).hexdigest(),
        "native_source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "native_header_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
        "harness_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "unicorn": U.__version__, "seed": args.seed, "builds": [],
        "limitations": "Scripted MMIO; DDR service replaced outside tested range; no mixer or full worker equivalence; busy loops bounded, polling counts/timing not compared"}
    try:
        with tempfile.TemporaryDirectory(prefix="repops-worker-verify-") as tmp:
            for opt in ("-O0", "-O2"):
                lib = Path(tmp) / (opt[1:] + (".dylib" if sys.platform == "darwin" else ".so"))
                subprocess.run(["cc", "-std=c11", opt, "-Wall", "-Wextra", "-Werror", "-fPIC",
                    "-dynamiclib" if sys.platform == "darwin" else "-shared", str(source), "-o", str(lib)],
                    check=True, capture_output=True, text=True)
                result = exercise(code, C.CDLL(str(lib)), vectors(args.seed))
                result["optimization"] = opt
                report["builds"].append(result)
                print(opt, result)
        report["status"] = "passed_bounded_drain_with_scripted_devices"
        report["total_cases"] = sum(b["cases_passed"] for b in report["builds"])
    except Exception as exc:
        report["status"] = "failed"
        report["error"] = str(exc)
        raise
    finally:
        (args.out / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Total {report['total_cases']}; only the drain/ACK interval was compared.")

if __name__ == "__main__":
    main()
