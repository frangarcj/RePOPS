#!/usr/bin/env python3
"""Differentially execute three hash-pinned POPSMAN leaf routines vs native C.

Unicorn executes original file bytes, not hand-encoded instruction fixtures.
Checks return values where defined, ordered writes, SYNC occurrence, final
mapped memory and unchanged K1. This does not emulate devices or prove memory
ordering/concurrency, whole-emulator equivalence, or all possible inputs.
"""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
import itertools
import json
import random
import subprocess
import sys
import tempfile
from pathlib import Path

from analyze_pops import Image
from map_popsman import POPSMAN_SHA256

try:
    import unicorn as U
    import unicorn.mips_const as M
except ImportError as exc:
    raise SystemExit("Install optional unicorn==2.1.4 in a project-local venv") from exc

ROOT = Path(__file__).resolve().parents[1]
START, STOP = 0x00100000, 0x00100FF0
PAGES = (0x1FC00000, 0x1D400000)
SENTINEL = b"\xa5" * 4096
WRITE = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32, C.c_uint32)
SYNC = C.CFUNCTYPE(None, C.c_void_p)


class Bus(C.Structure):
    _fields_ = [("context", C.c_void_p), ("write32", WRITE), ("sync", SYNC)]


CASES = (
    ("c93c56f8", 0x3590, 28, True),
    ("0babd960", 0x35AC, 44, True),
    ("e7f06e2b", 0x3ACC, 16, False),
)


def physical(address: int) -> int:
    # Only the known KSEG1 addresses in these three routines are normalized.
    if 0xA0000000 <= address < 0xC0000000:
        return address & 0x1FFFFFFF
    return address


def exercise(image: Image, native, vectors: list[tuple[int, int]], case: tuple) -> dict:
    name, entry, size, has_return = case
    code = image.bytes_at(entry, size)
    machine = U.Uc(U.UC_ARCH_MIPS, U.UC_MODE_MIPS32 | U.UC_MODE_LITTLE_ENDIAN)
    # These bounded routines only use MIPS32/r2 instructions shared by Allegrex.
    machine.ctl_set_cpu_model(M.UC_CPU_MIPS32_24KF)
    machine.mem_map(START, 4096)
    for page in PAGES:
        machine.mem_map(page, 4096)
    machine.mem_write(START, code)
    initial = machine.context_save()
    machine_events, native_events = [], []

    def on_write(uc, access, address, width, value, user_data):
        machine_events.append(["write", physical(address), width, value & 0xFFFFFFFF])

    def on_instruction(uc, address, width, user_data):
        # Record the instruction's occurrence, not a claim to emulate bus ordering.
        if uc.mem_read(address, 4) == b"\x0f\x00\x00\x00":
            machine_events.append(["sync"])

    machine.hook_add(U.UC_HOOK_MEM_WRITE, on_write)
    machine.hook_add(U.UC_HOOK_CODE, on_instruction)
    write_callback = WRITE(lambda context, address, value:
                           native_events.append(["write", physical(address), 4, value]))
    sync_callback = SYNC(lambda context: native_events.append(["sync"]))
    bus = Bus(None, write_callback, sync_callback)
    fn = getattr(native, "repops_pm_" + name)
    fn.argtypes = [C.POINTER(Bus), C.c_uint32] + ([C.c_uint32] if name == "0babd960" else [])
    fn.restype = C.c_uint32 if has_return else None

    for number, (argument, k1) in enumerate(vectors):
        machine.context_restore(initial)
        for page in PAGES:
            machine.mem_write(page, SENTINEL)
        machine.reg_write(M.UC_MIPS_REG_A0, argument)
        machine.reg_write(M.UC_MIPS_REG_K1, k1)
        machine.reg_write(M.UC_MIPS_REG_V0, 0x12345678)
        machine.reg_write(M.UC_MIPS_REG_RA, STOP)
        machine_events.clear()
        native_events.clear()
        # Budget and exact terminal PC catch runaway/unsupported execution.
        machine.emu_start(START, STOP, count=64)
        if machine.reg_read(M.UC_MIPS_REG_PC) != STOP:
            raise RuntimeError(f"{name}: did not return within instruction budget")
        result = fn(C.byref(bus), argument, k1) if name == "0babd960" else fn(C.byref(bus), argument)
        actual = machine.reg_read(M.UC_MIPS_REG_V0) & 0xFFFFFFFF
        matches = machine_events == native_events
        matches &= not has_return or result == actual
        matches &= machine.reg_read(M.UC_MIPS_REG_K1) == k1
        expected_pages = {page: bytearray(SENTINEL) for page in PAGES}
        for event in native_events:
            if event[0] != "write":
                continue
            _, address, width, value = event
            page = address & ~4095
            if page not in expected_pages or width != 4 or address % 4:
                raise RuntimeError(f"Unexpected C model write: {event}")
            offset = address - page
            expected_pages[page][offset:offset + 4] = value.to_bytes(4, "little")
        matches &= all(machine.mem_read(page, 4096) == contents
                       for page, contents in expected_pages.items())
        if not matches:
            failure = {"function": name, "case": number, "argument": hex(argument),
                       "k1": hex(k1), "binary_return": hex(actual), "native_return": result,
                       "binary_events": machine_events, "native_events": native_events}
            raise RuntimeError("Counterexample: " + json.dumps(failure))
    return {"nid": "0x" + name.upper(), "reference_entry": hex(entry),
            "reference_bytes": size, "reference_slice_sha256": hashlib.sha256(code).hexdigest(),
            "cases_passed": len(vectors), "defined_return_checked": has_return}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--elf", type=Path, default=ROOT / "build/f0-kd-popsman.prx")
    ap.add_argument("--out", type=Path, required=True, help="New result directory")
    ap.add_argument("--random-cases", type=int, default=1024)
    ap.add_argument("--seed", type=int, default=660)
    ap.add_argument("--cc", default="cc")
    args = ap.parse_args()
    if args.random_cases < 0:
        ap.error("--random-cases must be nonnegative")
    if args.out.exists():
        ap.error("Refusing to overwrite an existing result directory")
    image = Image(args.elf)
    if hashlib.sha256(image.data).hexdigest() != POPSMAN_SHA256:
        ap.error("Wrong firmware hash: this harness uses fixed, audited leaf ranges")
    args.out.mkdir(parents=True)
    edges = [0, 1, 2, 31, 32, 33, 0x1FFFFFFF, 0x20000000,
             0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF]
    vectors = list(itertools.product(edges, [0, 1, 0xFFFFF, 0x100000, 0xFFFFFFFF]))
    rng = random.Random(args.seed)
    vectors += [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(args.random_cases)]
    source = ROOT / "src/popsman_mailbox.c"
    report = {"firmware_sha256": POPSMAN_SHA256, "unicorn_version": U.__version__,
              "cpu_model": "MIPS32_24KF (only audited common instructions)",
              "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "seed": args.seed, "boundary_vectors_per_function": 60,
              "random_vectors_per_function": args.random_cases,
              "scope": "Returns (2 functions), ordered writes, SYNC occurrence, memory, K1; not devices/concurrency/full equivalence",
              "functions": [], "status": "incomplete"}
    try:
        with tempfile.TemporaryDirectory(prefix="repops-verify-") as tmp:
            library = Path(tmp) / ("mailbox.dylib" if sys.platform == "darwin" else "mailbox.so")
            command = [args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC",
                       "-dynamiclib" if sys.platform == "darwin" else "-shared",
                       str(source), "-o", str(library)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            native = C.CDLL(str(library))
            for case in CASES:
                result = exercise(image, native, vectors, case)
                report["functions"].append(result)
                print(f"{result['nid']}: {result['cases_passed']} binary-vs-C cases passed")
        report["status"] = "passed_for_tested_vectors"
        report["total_cases"] = sum(f["cases_passed"] for f in report["functions"])
    except Exception as exc:
        report["status"] = "failed"
        report["error"] = str(exc)
        raise
    finally:
        (args.out / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Total: {report['total_cases']}; report: {args.out / 'verification.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
