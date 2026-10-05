#!/usr/bin/env python3
"""Audit hash-pinned ME research windows. No firmware execution or byte export."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import capstone
from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN
from analyze_pops import Image

ROOT = Path(__file__).resolve().parents[1]


def checked_image(path: Path, expected: str) -> Image:
    # Reject a different firmware before trusting any fixed offsets.
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != expected:
        raise ValueError(f'Wrong firmware hash: {path.name}')
    return Image(path)


def measure(data: bytes, start: int) -> dict:
    if start < 0 or start % 4 or not data or len(data) % 4:
        raise ValueError('Expected a nonempty word-aligned window')
    decoder = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN)
    decoded, unsupported, indirect = 0, [], []
    for offset in range(0, len(data), 4):
        pc = start + offset
        instruction = next(decoder.disasm(data[offset:offset + 4], pc, count=1), None)
        if instruction is None:
            unsupported.append(f'0x{pc:08X}')
            continue
        decoded += 1
        if instruction.mnemonic in ('jr', 'jalr'):
            indirect.append({'address': f'0x{pc:08X}',
                             'mnemonic': instruction.mnemonic,
                             'operands': instruction.op_str})
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
            'decoded_words': decoded, 'unsupported_words': len(unsupported),
            'unsupported_addresses': unsupported, 'indirect_sites_including_returns': indirect,
            'interpretation': 'linear_window_scan_not_function_or_execution_coverage'}


def audit(spec: dict, pops: Path, provider: Path, ark: Path) -> dict:
    if spec.get('schema_version') != 1:
        raise ValueError('Unsupported target manifest schema')
    images = {'pops': checked_image(pops, spec['pops_sha256']),
              'provider': checked_image(provider, spec['provider_sha256'])}
    old = checked_image(ark, spec['ark_provider_sha256'])
    windows = []
    names = set()
    for target in spec['windows']:
        name, module = target['name'], target['module']
        if name in names or module not in images:
            raise ValueError('Duplicate target or unknown module')
        names.add(name)
        start, end = int(target['start'], 0), int(target['end_exclusive'], 0)
        if start < 0 or end <= start or end - start > 0x100000:
            raise ValueError('Invalid target window')
        data = images[module].bytes_at(start, end - start)
        row = dict(target, measurement=measure(data, start))
        if module == 'provider':
            previous = old.bytes_at(start, end - start)
            row['ark_raw_bytes_equal'] = data == previous
            row['ark_window_sha256'] = hashlib.sha256(previous).hexdigest()
        windows.append(row)
    return {'schema_version': 1, 'status': 'static_window_audit_only',
            'capstone_version': capstone.__version__,
            'input_hashes': {name: spec[name] for name in
                             ('pops_sha256', 'provider_sha256', 'ark_provider_sha256')},
            'windows': windows, 'limitations': spec['limitations']}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--targets', type=Path, default=ROOT / 'data/me_reverse_targets.json')
    parser.add_argument('--pops', type=Path, default=ROOT / 'build/pops_660.prx.dec')
    parser.add_argument('--provider', type=Path, default=ROOT / 'build/firmware_660/F0/kd/popsman.prx')
    parser.add_argument('--ark-provider', type=Path, default=ROOT / 'build/f0-kd-popsman.prx')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Refusing existing output directory')
    spec = json.loads(args.targets.read_text())
    result = audit(spec, args.pops, args.provider, args.ark_provider)
    result['targets_sha256'] = hashlib.sha256(args.targets.read_bytes()).hexdigest()
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / 'audit.json').write_text(json.dumps(result, indent=2) + '\n')
    for row in result['windows']:
        m = row['measurement']
        comparison = '' if 'ark_raw_bytes_equal' not in row else f"; ARK raw match={row['ark_raw_bytes_equal']}"
        print(f"{row['module']} {row['name']}: {m['bytes']} bytes, {m['unsupported_words']} unsupported words{comparison}")
    print('Static inventory only; no mixer or device behavior validated.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
