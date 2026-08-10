#!/usr/bin/env python3
"""Create a blank or preloaded 1MiB NDS retail-save sidecar."""

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
    parser.add_argument("rpg", type=Path, nargs="?")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument(
        "--blank", type=Path, metavar="OUTPUT",
        help="write an erased 1MiB sidecar without a PAL save slot",
    )
    parser.add_argument("--slot", type=int, default=1)
    args = parser.parse_args()

    if args.blank is not None:
        if args.rpg is not None or args.output is not None:
            parser.error("--blank cannot be combined with RPG or OUTPUT")
        output = args.blank
        payload = None
    else:
        if args.rpg is None or args.output is None:
            parser.error("RPG and OUTPUT are required unless --blank is used")
        output = args.output
        payload = args.rpg.read_bytes()

    if not 1 <= args.slot <= SLOT_COUNT:
        raise SystemExit(f"--slot must be between 1 and {SLOT_COUNT}")
    if payload is not None and (
        len(payload) < 2 or len(payload) > FOOTER_OFFSET
    ):
        raise SystemExit(
            f"RPG save is {len(payload)} bytes; capacity is {FOOTER_OFFSET}"
        )

    image = bytearray(b"\xFF" * SAVE_BYTES)
    if payload is not None:
        base = (args.slot - 1) * SLOT_BYTES
        image[base : base + len(payload)] = payload
        image[
            base + FOOTER_OFFSET : base + SLOT_BYTES
        ] = footer(args.slot, payload)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    if payload is None:
        print(f"wrote {output}: erased 1MiB retail save")
    else:
        saved_times = struct.unpack_from("<H", payload)[0]
        print(
            f"wrote {output}: slot={args.slot}, bytes={len(payload)}, "
            f"saved_times={saved_times}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
