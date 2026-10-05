#!/usr/bin/env python3
"""Compare POPSMAN DE630CD2 relocated instructions with the native C model.

The hardware helper at +0x35D8 is intercepted, recorded, and forced to return.
This validates only the caller's registration contract, NOT ME startup. Input
ELF, Ghidra export, and exact relocation provenance are checked before execution.
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
from map_popsman import POPSMAN_SHA256

ROOT = Path(__file__).resolve().parents[1]
EXPORT_SHA256 = "92fc7ac3d92919f4c0f321804fc51a24714c8a22e25fafc26c40040d21efb7a5"
ENTRY, HELPER, STOP = 0x3490, 0x35D8, 0x100000
CODE_ALIAS = 0x80000000
HI, LO, SLOT = 0x2F2C, 0x2F30, 0x4C5C
MODULE_SIZE, STACK_PAGE, STACK = 0x5000, 0x200000, 0x200FF0
READ = C.CFUNCTYPE(C.c_uint32, C.c_void_p, C.c_uint32)
WRITE = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32, C.c_uint32)
START = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32)


class Host(C.Structure):
    _fields_ = [("context", C.c_void_p), ("read32", READ),
                ("write32", WRITE), ("start", START)]


def checked_export(elf: Path, directory: Path) -> tuple[bytes, dict]:
    image = Image(elf)
    if hashlib.sha256(image.data).hexdigest() != POPSMAN_SHA256:
        raise ValueError("Wrong POPSMAN ELF hash")
    manifest = json.loads((directory / "manifest.json").read_text())
    expected = {"source_sha256": POPSMAN_SHA256, "language": "Allegrex:LE:32:default",
                "image_base": "00000000", "start": 0, "size": 0x3E60,
                "filename": "range.bin", "sha256": EXPORT_SHA256,
                "kind": "initialized_memory_after_ghidra_relocation_not_original_file_bytes"}
    for key, value in expected.items():
        if manifest.get(key) != value:
            raise ValueError(f"Unexpected Ghidra export field {key}")
    code = (directory / "range.bin").read_bytes()
    if len(code) != expected["size"] or hashlib.sha256(code).hexdigest() != EXPORT_SHA256:
        raise ValueError("Wrong relocated-code hash/length")
    applied = {int(r["address"], 16) for r in manifest["relocations"] if r["status"] == "APPLIED"}
    original = image.bytes_at(0, len(code))
    changed = [offset for offset in range(0, len(code), 4)
               if original[offset:offset + 4] != code[offset:offset + 4]]
    if any(offset not in applied for offset in changed):
        raise ValueError("Unexplained changed word outside applied relocations")
    return code, {"relocated_sha256": EXPORT_SHA256,
                  "changed_words": len(changed),
                  "registration_changed_offsets": [hex(o) for o in changed if ENTRY <= o < 0x3514],
                  "registration_slice_sha256": hashlib.sha256(code[ENTRY:0x3514]).hexdigest()}


def make_vectors(seed: int, count: int) -> list[dict]:
    values = [0, 1, 0x09FF8000, 0x7FFFFFFF, 0x80000000, 0xFFFF0000, 0xFFFFFFFE, 0xFFFFFFFF]
    vectors = [{"entry": entry, "stack": stack, "k1": k1, "hi": 0x3C1D0000,
                "lo": 0x37BD0000, "reset": True, "helper_return": 0}
               for entry, stack, k1 in itertools.product(values, values, [0, 1, 0xFFFFF, 0x100000, 0xFFFFFFFF])]
    rng = random.Random(seed)
    for _ in range(count):
        vectors.append({"entry": rng.getrandbits(32), "stack": rng.getrandbits(32),
                        "k1": rng.getrandbits(32), "hi": 0x3C1D0000 | rng.getrandbits(16),
                        "lo": 0x37BD0000 | rng.getrandbits(16), "reset": True,
                        "helper_return": rng.getrandbits(32)})
    for i in range(32):
        stack = (0x09FF8000 if i == 0 else 0x00010001 if i == 1 else rng.getrandbits(32))
        vectors.append({"entry": 0x1000 + i * 4, "stack": stack,
                        "k1": 0 if i < 2 else 0x100000, "hi": 0x3C1D0000,
                        "lo": 0x37BD0000, "reset": i == 0, "helper_return": 0xFFFFFFFF})
    return vectors


def exercise(code: bytes, native, vectors: list[dict]) -> dict:
    import unicorn as U
    import unicorn.mips_const as M

    machine = U.Uc(U.UC_ARCH_MIPS, U.UC_MODE_MIPS32 | U.UC_MODE_LITTLE_ENDIAN)
    machine.ctl_set_cpu_model(M.UC_CPU_MIPS32_24KF)
    machine.mem_map(0, MODULE_SIZE)
    machine.mem_map(STOP, 4096)
    machine.mem_map(STACK_PAGE, 4096)
    # Explicit mock outside the 132-byte function under test. The helper stub
    # loads a controlled V0 and returns using normal MIPS instructions.
    baseline = code + b"\xa5" * (MODULE_SIZE - len(code))
    machine.mem_write(0, baseline)
    initial = machine.context_save()
    binary_events, native_events, errors, trace = [], [], [], []
    native_memory = bytearray(baseline)
    helper_return = 0
    helper_calls = 0
    repeated_boundary_hooks = 0
    allowed = {HI, LO, SLOT}

    def word(address):
        return int.from_bytes(machine.mem_read(address, 4), "little")

    def on_read(uc, access, address, size, value, user):
        if address in {HI, LO}:
            if size != 4:
                raise RuntimeError("Unexpected read width")
            binary_events.append(["read", address, word(address)])
        elif not STACK_PAGE <= address < STACK_PAGE + 4096:
            raise RuntimeError(f"Unexpected read at {address:x}")

    def on_write(uc, access, address, size, value, user):
        if STACK_PAGE <= address < STACK_PAGE + 4096:
            return
        if address not in allowed or size != 4:
            raise RuntimeError(f"Unexpected write at {address:x}")
        binary_events.append(["write", address, value & 0xFFFFFFFF])

    def on_code(uc, address, size, user):
        nonlocal helper_calls, repeated_boundary_hooks
        address &= 0x1FFFFFFF
        trace.append(hex(address))
        if len(trace) > 20:
            del trace[0]
        if address == HELPER:
            snapshot = ["start_boundary", uc.reg_read(M.UC_MIPS_REG_K1),
                        word(SLOT), word(HI), word(LO)]
            prior = [event for event in binary_events if event[0] == "start_boundary"]
            if prior:
                if len(trace) >= 2 and trace[-2] == hex(HELPER) and prior == [snapshot]:
                    # A pre-instruction hook can be repeated at the same PC
                    # during translation. No intervening guest instruction or
                    # changed boundary state is allowed by this deduplication.
                    repeated_boundary_hooks += 1
                    return
                raise RuntimeError("Registration reached the helper more than once")
            helper_calls += 1
            if uc.reg_read(M.UC_MIPS_REG_A0) != word(HI) or uc.reg_read(M.UC_MIPS_REG_A1) != word(LO):
                raise RuntimeError("Unexpected A0/A1 at startup boundary: " + json.dumps({
                    "a0": hex(uc.reg_read(M.UC_MIPS_REG_A0)), "hi": hex(word(HI)),
                    "a1": hex(uc.reg_read(M.UC_MIPS_REG_A1)), "lo": hex(word(LO)),
                    "events": binary_events}))
            binary_events.append(snapshot)
            # Observation only: even non-PC register writes in a MIPS hook
            # can restart a translated block. The helper stub runs normally.
        elif not HELPER <= address < HELPER + 16 and not ENTRY <= address < 0x3514:
            raise RuntimeError(f"Execution escaped registration range: {address:x}")

    machine.hook_add(U.UC_HOOK_MEM_READ, on_read)
    machine.hook_add(U.UC_HOOK_MEM_WRITE, on_write)
    machine.hook_add(U.UC_HOOK_CODE, on_code)

    def read_native(context, address):
        if address not in {HI, LO}:
            errors.append(f"unexpected native read {address:x}")
            return 0
        value, = struct.unpack_from("<I", native_memory, address)
        native_events.append(["read", address, value])
        return value

    def write_native(context, address, value):
        if address not in allowed:
            errors.append(f"unexpected native write {address:x}")
            return
        struct.pack_into("<I", native_memory, address, value)
        native_events.append(["write", address, value])

    def start_native(context, k1):
        native_events.append(["start_boundary", k1] +
                             [struct.unpack_from("<I", native_memory, addr)[0] for addr in (SLOT, HI, LO)])

    callbacks = (READ(read_native), WRITE(write_native), START(start_native))
    host = Host(None, *callbacks)
    fn = native.repops_me_register
    fn.argtypes = [C.POINTER(Host), C.c_uint32, C.c_uint32, C.c_uint32]
    fn.restype = C.c_uint32
    preserved = (M.UC_MIPS_REG_S0, M.UC_MIPS_REG_S1, M.UC_MIPS_REG_S2, M.UC_MIPS_REG_S3,
                 M.UC_MIPS_REG_S4, M.UC_MIPS_REG_S5, M.UC_MIPS_REG_S6, M.UC_MIPS_REG_S7,
                 M.UC_MIPS_REG_FP)
    for number, vector in enumerate(vectors):
        machine.context_restore(initial)
        if vector["reset"]:
            native_memory[:] = baseline
            for address, value in ((HI, vector["hi"]), (LO, vector["lo"]), (SLOT, 0xABCD1234)):
                struct.pack_into("<I", native_memory, address, value)
            machine.mem_write(0, bytes(native_memory))
        binary_events.clear(); native_events.clear(); errors.clear(); trace.clear()
        machine.mem_write(STACK_PAGE, b"\x5a" * 4096)
        for i, reg in enumerate(preserved):
            machine.reg_write(reg, 0xCAFE0000 + i)
        machine.reg_write(M.UC_MIPS_REG_A0, vector["entry"])
        machine.reg_write(M.UC_MIPS_REG_A1, vector["stack"])
        machine.reg_write(M.UC_MIPS_REG_K1, vector["k1"])
        machine.reg_write(M.UC_MIPS_REG_SP, STACK)
        machine.reg_write(M.UC_MIPS_REG_RA, CODE_ALIAS | STOP)
        helper_return = vector["helper_return"]
        stub = struct.pack("<4I", 0x3C020000 | (helper_return >> 16),
                           0x34420000 | (helper_return & 0xFFFF), 0x03E00008, 0)
        native_memory[HELPER:HELPER + 16] = stub
        machine.mem_write(HELPER, stub)
        machine.ctl_remove_cache(HELPER, HELPER + 16)
        try:
            machine.emu_start(CODE_ALIAS | ENTRY, CODE_ALIAS | STOP, count=96)
        except Exception as exc:
            raise RuntimeError(f"Case {number}, {vector}, trace={trace}, PC={machine.reg_read(M.UC_MIPS_REG_PC):x}: {exc}") from exc
        if machine.reg_read(M.UC_MIPS_REG_PC) != (CODE_ALIAS | STOP):
            raise RuntimeError("Instruction budget exhausted before return")
        result = fn(C.byref(host), vector["entry"], vector["stack"], vector["k1"])
        checks = {
            "events": binary_events == native_events,
            "return": result == machine.reg_read(M.UC_MIPS_REG_V0),
            "module_memory": machine.mem_read(0, MODULE_SIZE) == native_memory,
            "k1": machine.reg_read(M.UC_MIPS_REG_K1) == vector["k1"],
            "stack_pointer": machine.reg_read(M.UC_MIPS_REG_SP) == STACK,
            "return_address": machine.reg_read(M.UC_MIPS_REG_RA) == (CODE_ALIAS | STOP),
            "preserved_registers": all(machine.reg_read(reg) == 0xCAFE0000 + i for i, reg in enumerate(preserved)),
            "native_callback_errors": not errors,
        }
        if not all(checks.values()):
            raise RuntimeError("Counterexample: " + json.dumps({"case": number, "input": vector,
                "checks": checks, "binary_events": binary_events, "native_events": native_events,
                "native_errors": errors}))
    return {"cases_passed": len(vectors), "accepted_start_boundaries": helper_calls,
            "rejected_calls": len(vectors) - helper_calls,
            "identical_consecutive_boundary_prehooks": repeated_boundary_hooks}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--elf", type=Path, default=ROOT / "build/f0-kd-popsman.prx")
    ap.add_argument("--relocated", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--random-cases", type=int, default=1024)
    ap.add_argument("--seed", type=int, default=660)
    ap.add_argument("--cc", default="cc")
    args = ap.parse_args()
    if args.random_cases < 0 or args.random_cases > 100000:
        ap.error("--random-cases must be between 0 and 100000")
    if args.out.exists():
        ap.error("Refusing to overwrite an existing result directory")
    code, provenance = checked_export(args.elf, args.relocated)
    try:
        import unicorn as U
    except ImportError:
        ap.error("Install optional unicorn==2.1.4 in a project-local venv")
    if U.__version__ != "2.1.4":
        ap.error("This validated harness pins unicorn==2.1.4")
    args.out.mkdir(parents=True)
    vectors = make_vectors(args.seed, args.random_cases)
    source, header = ROOT / "src/me_registration.c", ROOT / "src/me_registration.h"
    report = {"status": "incomplete", "input_sha256": POPSMAN_SHA256, "provenance": provenance,
              "harness_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "model_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "header_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
              "unicorn_version": U.__version__, "seed": args.seed,
              "execution_alias": "KSEG0: 0x80000000, physical/module data offsets unchanged",
              "helper_stub": {"replaced_offset": hex(HELPER), "bytes": 16,
                              "instructions": "LUI V0; ORI V0; JR RA; NOP (controlled test return)",
                              "registration_modified_after_ghidra_export": False},
              "boundary_cases": 320, "random_cases": args.random_cases, "stateful_cases": 32,
              "scope": "registration reads/writes, helper-call boundary, return, module memory and preserved registers",
              "excluded": "helper +0x35D8 is stubbed; no ME devices, timing, cache coherency, PSP/Vita boot, or full ABI equivalence",
              "builds": []}
    try:
        with tempfile.TemporaryDirectory(prefix="repops-me-verify-") as tmp:
            for optimization in ("-O0", "-O2"):
                library = Path(tmp) / ("registration.dylib" if sys.platform == "darwin" else "registration.so")
                command = [args.cc, "-std=c11", optimization, "-Wall", "-Wextra", "-Werror", "-fPIC",
                           "-dynamiclib" if sys.platform == "darwin" else "-shared", str(source), "-o", str(library)]
                # Separate library path prevents dlopen from reusing the first build.
                library = library.with_name(optimization[1:] + library.name)
                command[-1] = str(library)
                subprocess.run(command, check=True, capture_output=True, text=True)
                result = exercise(code, C.CDLL(str(library)), vectors)
                result["optimization"] = optimization
                report["builds"].append(result)
                print(optimization, result)
        report["status"] = "passed_for_tested_vectors_with_stubbed_helper"
        report["total_cases"] = sum(b["cases_passed"] for b in report["builds"])
    except Exception as exc:
        report["status"] = "failed"
        report["error"] = str(exc)
        raise
    finally:
        (args.out / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Total: {report['total_cases']}; helper is stubbed, not hardware-validated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
