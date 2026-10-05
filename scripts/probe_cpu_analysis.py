#!/usr/bin/env python3
"""Run reconstructed POPS +0x05154 on the first BIOS region.

Optional comparison executes the original analysis routine, not the PS1 BIOS,
in Unicorn. This is one focused sample, not full CPU/Allegrex validation.
"""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = '6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0'
IMAGE = '7e3fe7f349a9f45464708b564c67f1dd1c387fbe05ec898c8d82b60a074cac65'
BASE = 0x041B0000


def compare_original(image, directory, prepare=False, emission=False):
    import unicorn as U
    import unicorn.mips_const as M
    machine = U.Uc(U.UC_ARCH_MIPS, U.UC_MODE_MIPS32 | U.UC_MODE_LITTLE_ENDIAN)
    machine.ctl_set_cpu_model(M.UC_CPU_MIPS32_24KF)
    for address, size in ((0, 0x800000), (BASE, 0x20000),
                          (0x09C00000, 0x300000), (0x07000000, 0x10000)):
        machine.mem_map(address, size)
    machine.mem_write(0, image)
    # Scratchpad replaces unused module bytes only in this isolated executor.
    # The native implementation keeps these two address spaces separate.
    machine.mem_write(0x10000, bytes(0x4000))
    values = {0x130: 0x400000, 0x6F4: 0xFFFFFFFF, 0xB40: 0x28000,
              0xB48: (0x53C20 - 0xBFC00000) & 0xFFFFFFFF,
              0xB4C: BASE, 0xB50: 0xBFC00000, 0xB54: 0xBFC00C00}
    if prepare:
        values[0x1D0] = 0x09B80000
    for offset, value in values.items():
        machine.mem_write(0x10000 + offset, struct.pack('<I', value))
    for reg, value in ((M.UC_MIPS_REG_GP, 0x10000), (M.UC_MIPS_REG_SP, 0x0700F000),
                       (M.UC_MIPS_REG_A0, 0xBFC00000 if prepare else BASE), (M.UC_MIPS_REG_RA, 0x0700FFF0)):
        machine.reg_write(reg, value)
    stop = 0x5D5C if prepare else 0x0700FFF0
    machine.emu_start(0x58C0 if prepare else 0x5154, stop, count=200000)
    if machine.reg_read(M.UC_MIPS_REG_PC) != stop:
        raise RuntimeError('Original routine did not reach the selected boundary within the budget')
    emitted_code = None
    if emission:
        machine.mem_map(0x09B80000, 0x20000)
        # Skip the single CACHE at +0x5D5C; run the actual BIOS allocation-table
        # setup and the first category read through +0x5E77. Observe the next
        # instruction boundary explicitly; Unicorn's second `until` run can
        # leave a stale reported PC. The hook changes no guest register/memory.
        reached_setup = []
        def setup_boundary(uc, address, size, user):
            if address == 0x5E78:
                reached_setup.append(address)
                uc.emu_stop()
        hook = machine.hook_add(U.UC_HOOK_CODE, setup_boundary)
        machine.emu_start(0x5D60, 0x0700FFF0, count=200000)
        machine.hook_del(hook)
        if not reached_setup:
            raise RuntimeError(f'Original allocator setup did not reach boundary: PC={machine.reg_read(M.UC_MIPS_REG_PC):08X}')
        cursor = machine.reg_read(M.UC_MIPS_REG_S1)
        start = cursor
        high_water = struct.unpack('<I', machine.mem_read(0x10B4C, 4))[0]
        for record in range(BASE, high_water + 1, 16):
            category = struct.unpack('<H', machine.mem_read(record + 4, 2))[0]
            if category == 0:
                continue
            if category not in (9, 0x13):
                break
            for reg, value in ((M.UC_MIPS_REG_A0, category), (M.UC_MIPS_REG_A1, record),
                               (M.UC_MIPS_REG_A2, cursor), (M.UC_MIPS_REG_A3, 0),
                               (M.UC_MIPS_REG_SP, 0x0700F000), (M.UC_MIPS_REG_RA, 0x0700FFF0)):
                machine.reg_write(reg, value)
            machine.emu_start(0x6914, 0x0700FFF0, count=200000)
            if machine.reg_read(M.UC_MIPS_REG_PC) != 0x0700FFF0:
                raise RuntimeError('Original immediate emitter did not return')
            cursor = machine.reg_read(M.UC_MIPS_REG_V0)
        emitted_code = bytes(machine.mem_read(start, cursor - start))
    expected = bytes(machine.mem_read(BASE, 0xC010))
    native = (directory / 'records.bin').read_bytes().ljust(len(expected), b'\0')
    expected_scratch = bytes(machine.mem_read(0x10000, 0x4000))
    actual_scratch = (directory / 'scratch.bin').read_bytes()
    differences = [i for i, (a, b) in enumerate(zip(native, expected)) if a != b]
    report = {'unicorn_version': U.__version__, 'cpu_model': 'MIPS32_24KF',
              'sample': 'one initial BIOS analysis, PS1 instructions are not executed',
              'stage': '058C0_prefix_through_05D5B' if prepare else '05154_complete_call',
              'compared_record_bytes': len(expected), 'record_difference_count': len(differences),
              'first_differences': differences[:16],
              'scratch_equal': actual_scratch == expected_scratch,
              'record_length_equal': len(native) == len(expected),
              'high_water': struct.unpack_from('<I', expected_scratch, 0xB4C)[0],
              'image_sha256': IMAGE,
              'model_sha256': hashlib.sha256((ROOT / 'src/native/pops_analyze.c').read_bytes()).hexdigest()}
    if prepare:
        report['compiler_prefix_sha256'] = hashlib.sha256((ROOT / 'src/native/pops_compile.c').read_bytes()).hexdigest()
        report['original_emission_cursor_not_executable'] = machine.reg_read(M.UC_MIPS_REG_S1)
    if emission:
        report['stage'] = '06914_immediate_probe_after_original_register_setup'
        report['emitter_sha256'] = hashlib.sha256((ROOT / 'src/native/pops_emit.c').read_bytes()).hexdigest()
        report['allegrex_emitted_bytes'] = len(emitted_code)
        report['allegrex_bytes_equal'] = emitted_code == (directory / 'allegrex.bin').read_bytes()
        report['excluded'] = 'one setup CACHE skipped; full record walk/linking and execution not performed'
    report['passed'] = not differences and report['scratch_equal'] and report['record_length_equal']
    if emission:
        report['passed'] &= report['allegrex_bytes_equal']
    (directory / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    if not report['passed']:
        raise RuntimeError('Native/original analyzer mismatch; see comparison.json')
    print('Original/native records and scratchpad match for this BIOS analysis sample.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, default=ROOT / 'build/native_image')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--compare', action='store_true')
    parser.add_argument('--prepare', action='store_true', help='Include original compiler setup and cost pass before emission')
    parser.add_argument('--emit-immediates', action='store_true', help='Probe immediate emission after prepare; not full block compilation')
    args = parser.parse_args()
    if args.emit_immediates:
        args.prepare = True
    if args.out.exists():
        parser.error('Use a new output directory; earlier outputs are preserved')
    manifest = json.loads((args.image / 'manifest.json').read_text())
    image = (args.image / 'pops_image.bin').read_bytes()
    if (manifest.get('source_sha256') != SOURCE or manifest.get('image_sha256') != IMAGE or
            hashlib.sha256(image).hexdigest() != IMAGE):
        parser.error('Wrong POPS image/source fingerprint')
    subprocess.run(['make', 'analyze'], cwd=ROOT, check=True)
    args.out.mkdir(parents=True)
    command = [str(ROOT / 'build/repops-analyze'),
               str((args.image / 'pops_image.bin').resolve()), str(args.out.resolve())]
    if args.emit_immediates:
        command.append('--emit-immediates')
    elif args.prepare:
        command.append('--prepare')
    subprocess.run(command, check=True)
    blob = (args.out / 'records.bin').read_bytes()
    rows = []
    for offset in range(0, len(blob), 16):
        flags, dest, opcode, kind, boundary_cost, payload, rs, rt, auxiliary, cost = struct.unpack_from('<HBBHHIBBBB', blob, offset)
        if kind == 0:
            continue
        rows.append({'guest_pc': f'0x{0xBFC00000 + offset // 4:08X}', 'flags': f'0x{flags:04X}',
                     'destination': dest, 'opcode': f'0x{opcode:02X}', 'category': kind,
                     'payload': f'0x{payload:08X}', 'source1': rs, 'source2': rt,
                     'auxiliary': auxiliary, 'cost_field': cost, 'boundary_cost': boundary_cost})
    (args.out / 'records.json').write_text(json.dumps(rows, indent=2) + '\n')
    print(f'{len(rows)} nonempty record categories; includes boundary records, not executed instructions.')
    if args.compare:
        compare_original(image, args.out, args.prepare, args.emit_immediates)
    print('Output:', args.out)


if __name__ == '__main__':
    main()
