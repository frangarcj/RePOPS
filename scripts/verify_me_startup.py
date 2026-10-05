#!/usr/bin/env python3
"""Execute original relocated ME startup/control, with explicit PSP service mocks.

The 0x35D8 helper is NOT stubbed. Only PSP imports are redirected to test
trampolines. Devices are deterministic scripted memory, not real PSP hardware.
The C poll API yields pending rather than adding an invented firmware timeout.
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
from collections import Counter
from pathlib import Path
from typing import Callable

from verify_me_registration import checked_export, Host as RegistrationHost, START
from map_popsman import POPSMAN_SHA256

ROOT = Path(__file__).resolve().parents[1]
ALIAS = 0x80000000
MODULE_SIZE, STOP, STACK_PAGE, STACK = 0x5000, 0x100000, 0x200000, 0x200FF0
TRAMP, MAIL = 0x300000, 0x301000
ACK, CONTROL, CLOCK, BOOT = 0xBFC007F0, 0xBFC007F8, 0xBC100070, 0xBFC00040
SLOT, HI, LO = 0x4C5C, 0x2F2C, 0x2F30
SERVICE_ARGS = {0x3E04: 0, 0x3E1C: 0, 0x3E0C: 0, 0x3B84: 0, 0x3B8C: 1,
                0x3C4C: 3, 0x3CA4: 0, 0x3CFC: 1, 0x3E2C: 0, 0x3C84: 1, 0x3CEC: 1}
RANGES = ((0x3490, 0x3514), (0x3514, 0x3590), (0x35D8, 0x36D8))
READ = C.CFUNCTYPE(C.c_uint32, C.c_void_p, C.c_uint32)
WRITE = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32, C.c_uint32)
SERVICE = C.CFUNCTYPE(C.c_uint32, C.c_void_p, *([C.c_uint32] * 4))


class Host(C.Structure):
    _fields_ = [("context", C.c_void_p), ("read32", READ),
                ("write32", WRITE), ("service", SERVICE)]


class Wait(C.Structure):
    _fields_ = [("target", C.c_uint32), ("previous_ack_bit", C.c_uint32),
                ("phase", C.c_uint32)]


def physical(address):
    return address & 0x1FFFFFFF if 0x80000000 <= address < 0xC0000000 else address


def decode_word_access(word: int, read_gpr: Callable[[int], int]) -> tuple[str, int, int] | None:
    """Decode the audited LW/SW subset for observation, not execution.

    Return operation, effective address and value-register index. Reject other
    memory instructions rather than silently omitting a future access.
    """
    opcode = word >> 26
    if opcode not in (0x23, 0x2B):
        if 0x20 <= opcode <= 0x3F:
            raise ValueError(f'Unaudited memory/cache opcode: {word:08x}')
        return None
    rs, rt = (word >> 21) & 31, (word >> 16) & 31
    displacement = (word & 0xFFFF) - (0x10000 if word & 0x8000 else 0)
    base = read_gpr(rs) if rs else 0
    address = (base + displacement) & 0xFFFFFFFF
    return ('read' if opcode == 0x23 else 'write', address, rt)


def make_vectors(seed, random_count):
    rng = random.Random(seed)
    def vec(kind, **kw):
        v = dict(kind=kind, config=rng.getrandbits(32), mode=0,
                 clock=rng.getrandbits(32), token=rng.getrandbits(32),
                 service_return=rng.getrandbits(32), callback=0x08804000,
                 old_ack=rng.getrandbits(32), argument=1, acks=[1],
                 entry=0x08804000, stack=0x09FF8000, k1=0, pending=False)
        v.update(kw)
        return v
    result = []
    for config, mode, acks in itertools.product(
            (0, 1, 3, 0xFFFFFFFF), (0, 1, 2, 3, 4, 0xFFFFFFFF),
            ([1], [0, 1], [3, 0, 1])):
        result.append(vec("boot", config=config, mode=mode, acks=acks))
    for arg, callback, old, delay in itertools.product(
            (0, 1, 0x80000000, 0xFFFFFFFF), (0, 0x08804000),
            (0, 1, 3, 0xFFFFFFFF), (False, True)):
        target = int(arg != 0)
        result.append(vec("control", argument=arg, callback=callback, old_ack=old,
                          acks=([3, target] if delay else [target])))
    for entry, stack, k1 in itertools.product(
            (0, 0x08804000, 0x80000000, 0xFFFFFFFF),
            (0, 0x09FF8000, 0x80000000, 0xFFFFFFFF), (0, 0x100000)):
        result.append(vec("register_boot", entry=entry, stack=stack, k1=k1, acks=[3, 1]))
    for _ in range(random_count):
        n = rng.randrange(5)
        result.append(vec("boot", mode=rng.getrandbits(32), acks=[3] * n + [1], k1=rng.getrandbits(32)))
        arg = rng.choice([0, rng.getrandbits(32)])
        result.append(vec("control", argument=arg, callback=rng.choice([0, 0x08804000]),
                          acks=[3] * n + [int(arg != 0)], k1=rng.getrandbits(32)))
        result.append(vec("register_boot", entry=rng.getrandbits(32), stack=rng.getrandbits(32),
                          k1=rng.getrandbits(32), acks=[3] * n + [1]))
    result += [vec("boot", mode=2, acks=[2], pending=True),
               vec("boot", acks=[3], pending=True),
               vec("control", argument=0, acks=[3], pending=True),
               vec("control", argument=1, acks=[3], pending=True)]
    return result


def build_trampolines(code):
    if len(code) != 0x3E60:
        raise ValueError('Expected the complete bounded provider code export')
    patched = bytearray(code)
    tramp = bytearray(4096)
    common = TRAMP + 0x200
    for i, address in enumerate(SERVICE_ARGS):
        target = TRAMP + i * 16
        struct.pack_into("<II", patched, address, 0x08000000 | (target >> 2), 0)
        struct.pack_into("<4I", tramp, i * 16,
                         0x34190000 | address, 0x08000000 | (common >> 2), 0, 0)
    # Test-only imports report calls using a guest store, then load the reply
    # from memory and return normally. No PC/register mutation in Unicorn hooks.
    instructions = [0x3C180000 | (MAIL >> 16), 0x37180000 | (MAIL & 0xFFFF),
                    0xAF040000, 0xAF050004, 0xAF060008, 0xAF19000C, 0x8F020010]
    for reg in list(range(3, 16)) + [24, 25]:
        instructions.append(0x3400BEEF | (reg << 16))
    instructions += [0x03E00008, 0]
    struct.pack_into('<' + 'I' * len(instructions), tramp, 0x200, *instructions)
    for start, end in RANGES:
        if patched[start:end] != code[start:end]:
            raise ValueError("Test attempted to modify a routine under test")
    return bytes(patched) + b'\xa5' * (MODULE_SIZE - len(code)), bytes(tramp)


class Environment:
    def __init__(self, vector, memory):
        self.v, self.memory = vector, memory
        self.events = []
        self.delay_count = 0
        self.device_active = False
        self.k1 = vector['k1']
        self.errors = []

    def get(self, address, size=4):
        return self.memory.read(physical(address), size)

    def put(self, address, data):
        self.memory.write(physical(address), data)

    def word(self, address):
        return int.from_bytes(self.get(address), 'little')

    def store(self, address, value):
        self.put(address, struct.pack('<I', value & 0xFFFFFFFF))

    def setup(self):
        for address, key in ((0x4A08, 'config'), (0x4C68, 'mode'), (SLOT, 'callback'),
                             (ACK, 'old_ack'), (CLOCK, 'clock')):
            self.store(address, self.v[key])
        self.store(CONTROL, 0xDEAD0000)

    def read_event(self, address):
        p = physical(address)
        allowed = {0x4A08, 0x4C68, SLOT, HI, LO, physical(ACK), physical(CLOCK), physical(BOOT)}
        if p not in allowed:
            raise RuntimeError(f'Unexpected read {address:x}')
        value = self.word(address)
        self.events.append(['read', p, value])
        return value

    def write_event(self, address, value, do_write=True):
        p = physical(address)
        if p not in {HI, LO, SLOT, physical(CONTROL), physical(ACK), physical(CLOCK), physical(BOOT)}:
            raise RuntimeError(f'Unexpected write {address:x}')
        self.events.append(['write', p, value & 0xFFFFFFFF])
        if do_write:
            self.store(address, value)

    def service(self, id, a0, a1, a2):
        if id not in SERVICE_ARGS:
            raise RuntimeError(f'Unexpected service {id:x}')
        self.events.append(['service', id, *([a0, a1, a2][:SERVICE_ARGS[id]]), self.k1])
        if id == 0x3B84:
            return self.v['token']
        if id == 0x3C4C:
            if (a0, a1, a2) != (BOOT, 0x2F28, 0x60):
                raise RuntimeError('Unexpected memcpy arguments')
            self.put(a0, self.get(a1, a2))
        if id in (0x3E2C, 0x3CEC):
            self.device_active = True
            self.delay_count = 0
            self.store(ACK, self.v['acks'][0])
        if id == 0x3C84:
            if a0 != 100 or not self.device_active:
                raise RuntimeError('Invalid polling delay')
            self.delay_count += 1
            self.store(ACK, self.v['acks'][min(self.delay_count, len(self.v['acks']) - 1)])
        return self.v['service_return']


class NativeMemory:
    def __init__(self, baseline):
        self.pages = {0: bytearray(baseline), physical(ACK) & ~4095: bytearray(b'\xa5'*4096),
                      physical(CLOCK) & ~4095: bytearray(b'\xa5'*4096)}

    def locate(self, address, size):
        for start, blob in self.pages.items():
            if start <= address and address + size <= start + len(blob):
                return blob, address - start
        raise RuntimeError(f'Unmapped native address {address:x}, size {size}')

    def read(self, address, size):
        blob, offset = self.locate(address, size)
        return bytes(blob[offset:offset+size])

    def write(self, address, data):
        blob, offset = self.locate(address, len(data))
        blob[offset:offset+len(data)] = data


class BinaryMemory:
    def __init__(self, machine): self.machine = machine
    def read(self, address, size): return bytes(self.machine.mem_read(address, size))
    def write(self, address, data): self.machine.mem_write(address, bytes(data))


def native_run(native, env):
    def safe(fn, fallback=0):
        def wrapped(*args):
            try: return fn(*args)
            except Exception as exc:
                env.errors.append(str(exc))
                return fallback
        return wrapped
    callbacks = (READ(safe(lambda ctx, a: env.read_event(a))),
                 WRITE(safe(lambda ctx, a, v: env.write_event(a, v))),
                 SERVICE(safe(lambda ctx, id, a, b, c: env.service(id, a, b, c))))
    host = Host(None, *callbacks)
    wait = Wait()
    def boot():
        native.repops_me_boot_begin(C.byref(host), C.byref(wait))
    def poll():
        for _ in range(16):
            if native.repops_me_wait_step(C.byref(host), C.byref(wait)):
                return True
        return False
    kind = env.v['kind']
    if kind == 'register_boot':
        completed = True
        def start(ctx, k1):
            nonlocal completed
            env.k1 = k1
            boot()
            completed = poll()
        start_cb = START(safe(start))
        # ctypes callback signatures match the registration host exactly.
        from verify_me_registration import READ as RREAD, WRITE as RWRITE
        rr = RREAD(safe(lambda ctx, a: env.read_event(a)))
        rw = RWRITE(safe(lambda ctx, a, v: env.write_event(a, v)))
        reg_host = RegistrationHost(None, rr, rw, start_cb)
        value = native.repops_me_register(C.byref(reg_host), env.v['entry'], env.v['stack'], env.v['k1'])
        return completed, value
    if kind == 'boot': boot()
    else: native.repops_me_control_begin(C.byref(host), env.v['argument'], C.byref(wait))
    done = poll()
    return done, wait.previous_ack_bit if kind == 'control' else None


def configure_native(native):
    native.repops_me_boot_begin.argtypes = [C.POINTER(Host), C.POINTER(Wait)]
    native.repops_me_boot_begin.restype = None
    native.repops_me_control_begin.argtypes = [C.POINTER(Host), C.c_uint32, C.POINTER(Wait)]
    native.repops_me_control_begin.restype = None
    native.repops_me_wait_step.argtypes = [C.POINTER(Host), C.POINTER(Wait)]
    native.repops_me_wait_step.restype = C.c_bool
    native.repops_me_register.argtypes = [C.POINTER(RegistrationHost), C.c_uint32, C.c_uint32, C.c_uint32]
    native.repops_me_register.restype = C.c_uint32


def exercise(code, native, vectors):
    import unicorn as U
    import unicorn.mips_const as M
    baseline, tramp = build_trampolines(code)
    machine = U.Uc(U.UC_ARCH_MIPS, U.UC_MODE_MIPS32 | U.UC_MODE_LITTLE_ENDIAN)
    machine.ctl_set_cpu_model(M.UC_CPU_MIPS32_24KF)
    for address, size in ((0, MODULE_SIZE), (STACK_PAGE, 4096), (STOP, 4096),
                          (TRAMP, 8192), (physical(ACK) & ~4095, 4096),
                          (physical(CLOCK) & ~4095, 4096)):
        machine.mem_map(address, size)
    machine.mem_write(TRAMP, tramp)
    initial = machine.context_save()
    env = None
    trace = []
    gprs = [getattr(M, f'UC_MIPS_REG_{i}') for i in range(32)]
    def on_write(uc, access, address, size, value, user):
        p = physical(address)
        if size != 4 or p not in (MAIL, MAIL + 4, MAIL + 8, MAIL + 12):
            raise RuntimeError('Unexpected test-mailbox write')
        if p == MAIL + 12:
            args = struct.unpack('<3I', uc.mem_read(MAIL, 12))
            env.k1 = uc.reg_read(M.UC_MIPS_REG_K1)
            reply = env.service(value, *args)
            uc.mem_write(MAIL + 16, struct.pack('<I', reply))
    def on_code(uc, address, size, user):
        p = physical(address)
        trace.append(hex(p))
        if len(trace) > 20: del trace[0]
        in_firmware = any(start <= p < end for start, end in RANGES)
        if not (in_firmware or any(start <= p < start + 8 for start in SERVICE_ARGS) or
                TRAMP <= p < TRAMP + 4096):
            raise RuntimeError(f'Execution outside allowed ranges: {address:x}')
        if in_firmware:
            # The audited routines only use LW/SW for memory. Read-only
            # pre-instruction observation avoids Unicorn's MEM-hook delay-slot
            # restart issue. Final mapped memory is checked independently below.
            word, = struct.unpack_from('<I', code, p)
            access = decode_word_access(word, lambda reg: uc.reg_read(gprs[reg]))
            if access is not None:
                kind, address, rt = access
                if STACK_PAGE <= physical(address) < STACK_PAGE + 4096:
                    return
                if kind == 'read':
                    env.read_event(address)
                else:
                    env.write_event(address, uc.reg_read(gprs[rt]), do_write=False)
    # Only the synthetic service mailbox is memory-hooked, and its stores are
    # not in branch delay slots. No firmware instruction is modified.
    machine.hook_add(U.UC_HOOK_MEM_WRITE, on_write, begin=MAIL, end=MAIL + 15)
    machine.hook_add(U.UC_HOOK_CODE, on_code)
    configure_native(native)
    preserved = [M.UC_MIPS_REG_S0, M.UC_MIPS_REG_S1, M.UC_MIPS_REG_S2, M.UC_MIPS_REG_S3,
                 M.UC_MIPS_REG_S4, M.UC_MIPS_REG_S5, M.UC_MIPS_REG_S6, M.UC_MIPS_REG_S7, M.UC_MIPS_REG_FP]
    counts = Counter()
    for number, v in enumerate(vectors):
        machine.context_restore(initial)
        native_mem = NativeMemory(baseline)
        for address, blob in native_mem.pages.items(): machine.mem_write(address, bytes(blob))
        machine.mem_write(STACK_PAGE, b'\x5a' * 4096)
        machine.mem_write(MAIL, b'\0' * 4096)
        env = Environment(v, BinaryMemory(machine))
        other = Environment(v, native_mem)
        env.setup(); other.setup(); trace.clear()
        for i, reg in enumerate(preserved): machine.reg_write(reg, 0xCAFE0000 + i)
        for reg, value in ((M.UC_MIPS_REG_SP, STACK), (M.UC_MIPS_REG_RA, ALIAS | STOP),
                           (M.UC_MIPS_REG_K1, v['k1']), (M.UC_MIPS_REG_A0, v['entry'] if v['kind']=='register_boot' else v['argument']),
                           (M.UC_MIPS_REG_A1, v['stack'])):
            machine.reg_write(reg, value)
        entry = {'boot': 0x35D8, 'control': 0x3514, 'register_boot': 0x3490}[v['kind']]
        try:
            machine.emu_start(ALIAS | entry, ALIAS | STOP, count=4000)
            returned = machine.reg_read(M.UC_MIPS_REG_PC) == (ALIAS | STOP)
            completed, result = native_run(native, other)
            checks = {'expected_termination': returned == completed == (not v['pending']),
                      'native_callbacks': not other.errors,
                      'firmware_intact': all(bytes(machine.mem_read(a, b - a)) == code[a:b]
                                             for a, b in RANGES)}
            if not v['pending']:
                checks['ordered_events'] = env.events == other.events
                checks['memory'] = all(bytes(machine.mem_read(a, len(b))) == b for a,b in native_mem.pages.items())
                checks['preserved_registers'] = all(machine.reg_read(reg)==0xCAFE0000+i for i,reg in enumerate(preserved))
                checks['k1'] = machine.reg_read(M.UC_MIPS_REG_K1) == v['k1']
                checks['sp'] = machine.reg_read(M.UC_MIPS_REG_SP) == STACK
                checks['ra'] = machine.reg_read(M.UC_MIPS_REG_RA) == ALIAS | STOP
                if result is not None: checks['return'] = result == machine.reg_read(M.UC_MIPS_REG_V0)
            else:
                # The emulator budget may stop mid-iteration. Compare a complete
                # 16-poll native prefix, without asserting callee epilogue state.
                checks['pending_prefix'] = len(env.events) >= len(other.events) and env.events[:len(other.events)] == other.events
            if not all(checks.values()):
                raise RuntimeError(json.dumps({'checks': checks, 'native_errors': other.errors,
                    'binary_return': machine.reg_read(M.UC_MIPS_REG_V0), 'native_return': result,
                    'binary_events': env.events[:70], 'native_events': other.events[:70]}))
            counts[v['kind'] + ('_pending_prefix' if v['pending'] else '_returned')] += 1
        except Exception as exc:
            raise RuntimeError(f'Case {number}: {v}; PC={machine.reg_read(M.UC_MIPS_REG_PC):x}; trace={trace}; {exc}') from exc
    return dict(counts)


def check_negative_controls(code, native, vectors):
    """Deliberately break two instructions in test memory; require a mismatch.

    These runs are separate from positive comparisons and never change the ELF
    or Ghidra export. An emulator exception does not count as a detected error.
    """
    controls = [(0x36D4, 'wrong_control_store', [1]),
                (0x369C, 'inverted_ack_branch', [3, 0, 1])]
    results = []
    for offset, name, replies in controls:
        vector = next(v for v in vectors if v['kind'] == 'boot' and v['mode'] == 0
                      and v['acks'] == replies and not v['pending'])
        mutant = bytearray(code)
        old, = struct.unpack_from('<I', mutant, offset)
        if offset == 0x36D4:
            if old >> 26 != 0x2B or (old >> 16) & 31 != 13:
                raise ValueError('Unexpected control-store instruction')
            new = old & ~(31 << 16)  # Store zero instead of T5 (one).
        else:
            if old >> 26 != 4:
                raise ValueError('Unexpected ACK comparison branch')
            new = old ^ (1 << 26)  # BEQ -> BNE.
        struct.pack_into('<I', mutant, offset, new)
        try:
            exercise(bytes(mutant), native, [vector])
        except RuntimeError as exc:
            if '"ordered_events": false' not in str(exc):
                raise RuntimeError('Negative control failed for an unexpected reason') from exc
            results.append({'name': name, 'offset': hex(offset),
                            'status': 'rejected_by_ordered_event_comparison'})
        else:
            raise RuntimeError(f'Negative control was incorrectly accepted: {name}')
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf', type=Path, default=ROOT/'build/f0-kd-popsman.prx')
    ap.add_argument('--relocated', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--seed', type=int, default=660)
    ap.add_argument('--random-cases', type=int, default=128, help='Random vectors per operation')
    ap.add_argument('--cc', default='cc')
    args = ap.parse_args()
    if args.out.exists(): ap.error('Refusing an existing result directory')
    if not 0 <= args.random_cases <= 10000: ap.error('Invalid random-case count')
    code, provenance = checked_export(args.elf, args.relocated)
    import unicorn as U
    if U.__version__ != '2.1.4': ap.error('Harness requires pinned Unicorn 2.1.4')
    args.out.mkdir(parents=True)
    vectors = make_vectors(args.seed, args.random_cases)
    files = ['src/me_startup.c', 'src/me_startup.h', 'src/me_registration.c', 'src/me_registration.h',
             'scripts/verify_me_startup.py', 'scripts/verify_me_registration.py']
    report = dict(status='incomplete', elf_sha256=POPSMAN_SHA256, provenance=provenance,
        unicorn_version=U.__version__, seed=args.seed, vectors=len(vectors), builds=[],
        source_hashes={f: hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},
        tested_ranges=[{'start': hex(a), 'bytes': b-a, 'sha256': hashlib.sha256(code[a:b]).hexdigest()} for a,b in RANGES],
        helper_35d8_stubbed=False, routine_bytes_modified=False,
        service_stub_offsets=[hex(a) for a in SERVICE_ARGS],
        service_stub_bytes=8,
        service_stub_shape='J trampoline; NOP (original import slots only)',
        access_observation='read-only LW/SW pre-instruction hooks; final memory also compared',
        scope='Ordered reads/writes and PSP-service boundaries, mapped memory, returned state, bounded pending prefixes',
        excluded='PSP services/device implementation, ME execution, cache coherency, timing, whole-emulator correctness or Vita boot')
    try:
        with tempfile.TemporaryDirectory(prefix='repops-startup-') as tmp:
            for optimization in ('-O0', '-O2'):
                lib = Path(tmp)/(optimization[1:] + ('.dylib' if sys.platform=='darwin' else '.so'))
                subprocess.run([args.cc,'-std=c11',optimization,'-Wall','-Wextra','-Werror','-fPIC',
                    '-dynamiclib' if sys.platform=='darwin' else '-shared',str(ROOT/'src/me_startup.c'),
                    str(ROOT/'src/me_registration.c'),'-o',str(lib)], check=True, capture_output=True, text=True)
                native = C.CDLL(str(lib))
                counts = exercise(code, native, vectors)
                negative_controls = check_negative_controls(code, native, vectors)
                report['builds'].append({'optimization': optimization, 'counts': counts,
                                        'cases_passed': sum(counts.values()),
                                        'separate_negative_controls': negative_controls})
                print(optimization, counts, flush=True)
        report['status']='passed_with_scripted_service_and_device_responses'
        report['total_cases']=sum(b['cases_passed'] for b in report['builds'])
    except Exception as exc:
        report['status']='failed'; report['error']=str(exc)
        raise
    finally:
        (args.out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Total:',report['total_cases'],'No PSP device or ME thread was executed.')


if __name__ == '__main__':
    main()
