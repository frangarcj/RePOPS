#!/usr/bin/env python3
"""Build one functional PSP C replacement into the hash-pinned local POPS PRX.

This is a hybrid candidate, not a full-source rebuild or an on-device test.
The remainder of the firmware, including PRX relocation tables, stays identical.
No firmware bytes are embedded in this script or distributed with the sources.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import random
import shutil
import struct
import subprocess
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from analyze_pops import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA256 = '6a4aea3f731336916db97194c1a27983c18297c2dfcb1a1a328fd4ff8b09c8e0'
OFFSET, CAPACITY = 0x44DC, 40
SYMBOL = 'repops_emit_t9_decrement'
FLAGS = ['-Os', '-march=allegrex', '-mabi=eabi', '-G0', '-ffreestanding',
         '-fno-builtin', '-fno-pic', '-mno-abicalls']


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def check_input(path: Path, audit: Path) -> tuple[Image, bytes]:
    image = Image(path)
    if sha256(image.data) != SOURCE_SHA256:
        raise ValueError('Wrong original POPS hash; refusing a different firmware')
    manifest = json.loads((audit / 'manifest.json').read_text())
    required = {'source_sha256': SOURCE_SHA256, 'language': 'Allegrex:LE:32:default',
                'image_base': '00000000', 'start': OFFSET, 'size': CAPACITY,
                'filename': 'range.bin',
                'kind': 'initialized_memory_after_ghidra_relocation_not_original_file_bytes'}
    if any(manifest.get(k) != v for k, v in required.items()):
        raise ValueError('Wrong Ghidra range provenance')
    original = image.bytes_at(OFFSET, CAPACITY)
    exported = (audit / 'range.bin').read_bytes()
    if exported != original or sha256(exported) != manifest.get('sha256'):
        raise ValueError('Ghidra range does not match original function bytes')
    if manifest.get('relocations') != []:
        raise ValueError('The in-place function must have no PRX relocation entries')
    return image, original


def extract_function(obj: Path) -> bytes:
    with obj.open('rb') as fp:
        elf = ELFFile(fp)
        if elf.elfclass != 32 or not elf.little_endian or elf.header.e_machine != 'EM_MIPS':
            raise ValueError('Compiler did not produce a 32-bit little-endian MIPS object')
        table = elf.get_section_by_name('.symtab')
        symbols = table.get_symbol_by_name(SYMBOL) if table else None
        if not symbols or len(symbols) != 1:
            raise ValueError('Missing or ambiguous replacement symbol')
        symbol = symbols[0]
        index, start, size = symbol['st_shndx'], symbol['st_value'], symbol['st_size']
        if symbol['st_info']['type'] != 'STT_FUNC' or not isinstance(index, int):
            raise ValueError('Replacement is not a defined function')
        if not 0 < size <= CAPACITY or size % 4 or start % 4:
            raise ValueError(f'Replacement does not fit the audited {CAPACITY}-byte slot: {size}')
        section = elf.get_section(index)
        if not section['sh_flags'] & 4 or start + size > section['sh_size']:
            raise ValueError('Invalid executable symbol bounds')
        for relocations in elf.iter_sections():
            if isinstance(relocations, RelocationSection) and relocations['sh_info'] == index:
                if any(start <= r['r_offset'] < start + size for r in relocations.iter_relocations()):
                    raise ValueError('Replacement contains unresolved code relocations')
        code = section.data()[start:start + size]
    # A position-dependent J/JAL cannot be copied unchanged into the slot.
    for word, in struct.iter_unpack('<I', code):
        if word >> 26 in (2, 3):
            raise ValueError('Absolute J/JAL is not supported by this in-place leaf builder')
    return code


def validate(original: bytes, replacement: bytes, count: int, seed: int) -> dict:
    import unicorn as U
    import unicorn.mips_const as M
    if U.__version__ != '2.1.4':
        raise ValueError('The execution checks pin unicorn==2.1.4')
    start, stop, data, output, stack = 0x100000, 0x100FF0, 0x200000, 0x200800, 0x300FF0
    machines = []
    preserved = [getattr(M, 'UC_MIPS_REG_' + name) for name in
                 ['S0', 'S1', 'S2', 'S3', 'S4', 'S5', 'S6', 'S7', 'GP', 'FP']]
    for code in (original, replacement):
        m = U.Uc(U.UC_ARCH_MIPS, U.UC_MODE_MIPS32 | U.UC_MODE_LITTLE_ENDIAN)
        m.ctl_set_cpu_model(M.UC_CPU_MIPS32_24KF)
        for address in (start, data, stack & ~4095):
            m.mem_map(address, 4096)
        m.mem_write(start, code)
        machines.append((m, m.context_save()))
    edges = [0, 1, 2, 32767, 32768, 65535, 65536, 65537, 0x7FFFFFFF,
             0x80000000, 0x80000001, 0xFFFFFFFE, 0xFFFFFFFF]
    rng = random.Random(seed)
    vectors = edges + [rng.getrandbits(32) for _ in range(count)]
    sentinel = b'\xa5' * 4096
    for case, amount in enumerate(vectors):
        observations = []
        for m, initial in machines:
            m.context_restore(initial)
            m.mem_write(data, sentinel)
            m.reg_write(M.UC_MIPS_REG_A0, amount)
            m.reg_write(M.UC_MIPS_REG_A1, output)
            m.reg_write(M.UC_MIPS_REG_SP, stack)
            m.reg_write(M.UC_MIPS_REG_RA, stop)
            for index, reg in enumerate(preserved):
                m.reg_write(reg, 0xCAFE0000 + index)
            m.emu_start(start, stop, count=64)
            if m.reg_read(M.UC_MIPS_REG_PC) != stop:
                raise RuntimeError(f'Case {case} exhausted its instruction budget')
            observations.append((m.reg_read(M.UC_MIPS_REG_V0), bytes(m.mem_read(data, 4096)),
                                 m.reg_read(M.UC_MIPS_REG_SP), m.reg_read(M.UC_MIPS_REG_RA),
                                 [m.reg_read(r) for r in preserved]))
        expected = bytearray(sentinel)
        positive = 0 < amount < 0x80000000
        if positive:
            struct.pack_into('<I', expected, output - data, 0x27390000 | ((-amount) & 0xFFFF))
        reference = (output + (4 if positive else 0), bytes(expected), stack, stop,
                     [0xCAFE0000 + i for i in range(len(preserved))])
        if observations[0] != observations[1] or observations[1] != reference:
            raise RuntimeError(f'Counterexample at case {case}, amount=0x{amount:08x}')
    return {'cases_passed': len(vectors), 'boundary_cases': len(edges), 'random_cases': count,
            'seed': seed, 'engine': 'Unicorn 2.1.4 MIPS32_24KF; common audited instructions',
            'compared': 'Original MIPS vs psp-gcc MIPS: return, whole output page, SP/RA and callee-saved GPRs',
            'excluded': 'Whole-program equivalence, unknown mid-function entries, code introspection, hardware timing, PSP loading'}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, default=ROOT / 'build/pops_660.prx.dec')
    parser.add_argument('--audit', type=Path, required=True, help='Ghidra 0x44DC/40-byte range export')
    parser.add_argument('--out', type=Path, required=True, help='New output directory')
    parser.add_argument('--cc', default='psp-gcc')
    parser.add_argument('--random-cases', type=int, default=4096)
    parser.add_argument('--seed', type=int, default=660)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Refusing an existing output directory')
    if not 0 <= args.random_cases <= 100000:
        parser.error('Invalid random-case count')
    compiler = shutil.which(args.cc)
    if not compiler:
        parser.error('PSP compiler not found')
    image, original = check_input(args.elf, args.audit)
    source = ROOT / 'src/psp/emit_t9_decrement.c'
    args.out.mkdir(parents=True)
    report = {'status': 'incomplete', 'target': 'PSP / Allegrex; hybrid PRX, not full-source reconstruction',
              'original_sha256': SOURCE_SHA256, 'source_sha256': sha256(source.read_bytes()),
              'compiler': compiler, 'flags': FLAGS, 'function_offset': OFFSET, 'slot_bytes': CAPACITY}
    try:
        obj = args.out / 'emit_t9_decrement.o'
        subprocess.run([compiler, *FLAGS, '-c', str(source), '-o', str(obj)], check=True,
                       capture_output=True, text=True)
        report['compiler_version'] = subprocess.check_output([compiler, '--version'], text=True).splitlines()[0]
        replacement = extract_function(obj)
        report['replacement_bytes'] = len(replacement)
        report['validation'] = validate(original, replacement, args.random_cases, args.seed)
        offset = image.vaddr_to_offset(OFFSET)
        patch = replacement.ljust(CAPACITY, b'\0')
        result = image.data[:offset] + patch + image.data[offset + CAPACITY:]
        assert len(result) == len(image.data)
        assert result[:offset] == image.data[:offset] and result[offset + CAPACITY:] == image.data[offset + CAPACITY:]
        output = args.out / 'repops_660.hybrid.prx'
        output.write_bytes(result)
        report.update(status='hybrid_candidate_built_not_tested_on_PSP', output_file=output.name,
                      output_sha256=sha256(result), file_bytes=len(result),
                      changed_bytes=sum(a != b for a, b in zip(original, patch)),
                      unchanged_outside_slot=True, prx_relocation_tables_unchanged=True,
                      original_function_sha256=sha256(original), replacement_sha256=sha256(replacement),
                      original_module_name_preserved=True)
        print(f'Built {output}: {len(replacement)} C-compiled bytes in {CAPACITY}-byte slot')
        print(f"Changed {report['changed_bytes']} bytes; all remaining firmware bytes preserved")
        print(f"{report['validation']['cases_passed']} original-vs-compiled MIPS cases passed")
        print('PSP loading/gameplay not tested. Original firmware is still required.')
    except Exception as exc:
        report['status'], report['error'] = 'failed', str(exc)
        raise
    finally:
        (args.out / 'build_manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
