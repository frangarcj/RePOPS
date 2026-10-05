#!/usr/bin/env python3
"""Print a bounded raw-PRX MIPS range; unsupported Allegrex words stay explicit.

Addresses are unrelocated virtual addresses, not file offsets. This is a
cross-check of the original bytes, not a replacement for the Allegrex loader.
"""
import argparse
import struct
from pathlib import Path
from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN
from analyze_pops import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("start", type=lambda x: int(x, 0))
    parser.add_argument("size", type=lambda x: int(x, 0))
    args = parser.parse_args()
    if args.start % 4 or args.size % 4 or not 0 < args.size <= 65536:
        parser.error("use 4-byte alignment and a size between 4 and 65536")
    data = Image(args.elf).bytes_at(args.start, args.size)
    decoder = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN)
    for offset in range(0, len(data), 4):
        raw = data[offset:offset + 4]
        word, = struct.unpack("<I", raw)
        instruction = next(decoder.disasm(raw, args.start + offset, count=1), None)
        text = (f"{instruction.mnemonic} {instruction.op_str}" if instruction
                else ".word (unsupported by Capstone MIPS32)")
        print(f"{args.start + offset:08X}  {word:08X}  {text}")


if __name__ == "__main__":
    main()
