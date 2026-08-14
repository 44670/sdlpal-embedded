#!/usr/bin/env python3
"""Finalize ndstool output as an NTR-only DLDI homebrew image."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


HEADER_SIZE = 0x4000
HEADER_CRC_END = 0x15E
TWL_HEADER_START = 0x160
ROM_ALIGNMENT = 0x200
DLDI_MAGIC = b"\xED\xA5\x8D\xBF Chishm"
SECURE_AREA_END = 0x8000
DECRYPTED_SECURE_MARKER = (0xE7FFDEFF, 0xE7FFDEFF)


def crc16(data: bytes | bytearray, initial: int = 0xFFFF) -> int:
    value = initial
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xA001 if value & 1 else 0)
    return value


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & -alignment


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    image = bytearray(args.input.read_bytes())
    if len(image) < HEADER_SIZE:
        raise SystemExit("ndstool image is shorter than the NTR header")
    if image[0x0C:0x10] != b"####" or image[0x10:0x12] != b"00":
        raise SystemExit("ndstool did not emit the homebrew ####/00 identity")
    if struct.unpack_from("<I", image, 0x84)[0] != HEADER_SIZE:
        raise SystemExit("ndstool did not emit a full 0x4000 header")
    if DLDI_MAGIC not in image:
        raise SystemExit("linked image has no standard DLDI patch target")

    ntr_end = struct.unpack_from("<I", image, 0x80)[0]
    if not HEADER_SIZE <= ntr_end <= len(image):
        raise SystemExit("invalid NTR application end offset")
    arm9_rom_offset = struct.unpack_from("<I", image, 0x20)[0]
    if not HEADER_SIZE <= arm9_rom_offset <= SECURE_AREA_END - 8:
        raise SystemExit("ARM9 does not leave room for a homebrew secure marker")

    image[0x12] = 0x00
    image[TWL_HEADER_START:HEADER_SIZE] = bytes(
        HEADER_SIZE - TWL_HEADER_START
    )
    # Pico Loader treats a matching secure-area CRC without this marker as an
    # encrypted retail secure area and attempts to load Blowfish keys. Stamp
    # the standard already-decrypted marker in the unused pre-entry padding.
    # Keep ndstool's original multiboot checksum: it then cannot match these
    # stamped bytes, so Pico Loader also bypasses its encrypted-retail path.
    struct.pack_into("<II", image, arm9_rom_offset, *DECRYPTED_SECURE_MARKER)
    final_size = align_up(ntr_end, ROM_ALIGNMENT)
    image = image[:final_size]
    struct.pack_into("<H", image, 0x15E, crc16(image[:HEADER_CRC_END]))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(f"NTR DLDI homebrew image: bytes={len(image)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
