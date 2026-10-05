#!/usr/bin/env python3
"""Read bounded PBP/PSISOIMG metadata, without modifying or extracting game data."""
import argparse
import json
import struct
from pathlib import Path

HEADER_SIZE = 0xB3C80


def inspect(path: Path) -> dict:
    size = path.stat().st_size
    with path.open('rb') as stream:
        header = stream.read(40)
        if len(header) != 40 or header[:4] != b'\0PBP':
            raise ValueError('Not a PBP header')
        offsets = struct.unpack_from('<8I', header, 8)
        if offsets[0] < 40 or any(a > b for a, b in zip(offsets, offsets[1:])) or offsets[-1] > size:
            raise ValueError('Invalid PBP component offsets')
        if offsets[7] - offsets[6] < 4:
            raise ValueError('Truncated DATA.PSP component')
        stream.seek(offsets[6])
        tag = stream.read(4)
        if len(tag) != 4:
            raise ValueError('Truncated DATA.PSP tag')
        stream.seek(offsets[7])
        data = stream.read(HEADER_SIZE)
    if len(data) != HEADER_SIZE or data[:12] != b'PSISOIMG0000':
        raise ValueError('Expected complete single-disc PSISOIMG header')
    fields = {hex(offset): struct.unpack_from('<I', data, offset)[0]
              for offset in (0x420, 0x424, 0xBFC, 0xC04, 0x1220, 0x1224,
                             0x12B4, 0x12B8, 0x12D4, 0x12D8)}
    return {'file_bytes': size, 'component_offsets': offsets,
            'data_psp_word': int.from_bytes(tag, 'little'),
            'provider_result_word': int.from_bytes(tag, 'little') ^ 0x4A08B53F,
            'header_bytes': len(data),
            'extended_prefix_hex': data[0x400:0x410].hex(),
            'disc_id_ascii': data[0x400:0x420].split(b'\0', 1)[0].decode('ascii', 'replace'),
            'toc_prefix_hex': data[0x800:0x830].hex(), 'fields': fields,
            'first_block': {'offset': struct.unpack_from('<I', data, 0x4000)[0],
                            'size': struct.unpack_from('<H', data, 0x4004)[0]}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pbp', type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.pbp), indent=2))


if __name__ == '__main__':
    main()
