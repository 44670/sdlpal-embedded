#!/usr/bin/env python3
"""Wrap one standard PAL RPG save in the NDS Slot-1 save layout."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


SAVE_BYTES = 1024 * 1024
SLOT_COUNT = 5
SLOT_BYTES = 0x30000
FOOTER_BYTES = 64
FOOTER_OFFSET = SLOT_BYTES - FOOTER_BYTES
MAGIC = b"PALNDSV1"
VERSION = 1
COMMITTED = 0x53415645


def footer(slot: int, payload: bytes) -> bytes:
    prefix = struct.pack(
        "<8sIIIII",
        MAGIC,
        VERSION,
        slot,
        1,
        len(payload),
        zlib.crc32(payload),
    )
    header_crc = zlib.crc32(prefix)
    result = prefix + struct.pack("<II28x", header_crc, COMMITTED)
    if len(result) != FOOTER_BYTES:
        raise AssertionError("NDS save footer ABI changed")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rpg", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--slot", type=int, default=1)
    args = parser.parse_args()

    if not 1 <= args.slot <= SLOT_COUNT:
        raise SystemExit(f"--slot must be between 1 and {SLOT_COUNT}")
    payload = args.rpg.read_bytes()
    if len(payload) < 2 or len(payload) > FOOTER_OFFSET:
        raise SystemExit(
            f"RPG save is {len(payload)} bytes; capacity is {FOOTER_OFFSET}"
        )

    image = bytearray(b"\xFF" * SAVE_BYTES)
    base = (args.slot - 1) * SLOT_BYTES
    image[base : base + len(payload)] = payload
    image[
        base + FOOTER_OFFSET : base + SLOT_BYTES
    ] = footer(args.slot, payload)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    saved_times = struct.unpack_from("<H", payload)[0]
    print(
        f"wrote {args.output}: slot={args.slot}, bytes={len(payload)}, "
        f"saved_times={saved_times}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
