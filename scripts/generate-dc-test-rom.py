#!/usr/bin/env python3
"""Generate a minimal valid GBA ROM image for Dreamcast smoke tests."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def write_rom(
    path: Path,
    size: int,
    title: str,
    code: str,
    maker: str,
    sram_signature: bool,
) -> None:
    rom = bytearray([0xFF] * size)
    rom[0:4] = struct.pack("<I", 0xEAFFFFFE)  # ARM infinite loop at 0x08000000

    title_bytes = title.encode("ascii", "ignore")[:12].ljust(12)
    code_bytes = code.encode("ascii", "ignore")[:4].ljust(4)
    maker_bytes = maker.encode("ascii", "ignore")[:2].ljust(2)

    rom[0xA0:0xAC] = title_bytes
    rom[0xAC:0xB0] = code_bytes
    rom[0xB0:0xB2] = maker_bytes
    rom[0xB2] = 0x96

    checksum = 0
    for index in range(0xA0, 0xBD):
        checksum = (checksum - rom[index]) & 0xFF
    checksum = (checksum - 0x19) & 0xFF
    rom[0xBD] = checksum

    if sram_signature and size > 0x100008:
        rom[0x100000:0x100008] = b"SRAM_V10"

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(rom)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--size-kib", type=int, default=0)
    parser.add_argument("--size-mib", type=int, default=0)
    parser.add_argument("--title", default="TEST ROM")
    parser.add_argument("--code", default="TEST")
    parser.add_argument("--maker", default="00")
    parser.add_argument("--sram-signature", action="store_true")
    args = parser.parse_args()

    if args.size_mib:
        size = args.size_mib * 1024 * 1024
    elif args.size_kib:
        size = args.size_kib * 1024
    else:
        size = 512 * 1024

    write_rom(
        args.output,
        size,
        args.title,
        args.code,
        args.maker,
        args.sram_signature,
    )
    print(f"Wrote {args.output} ({size} bytes)")


if __name__ == "__main__":
    main()
