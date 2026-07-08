#!/usr/bin/env python3
"""Compare DOS PAL save files while ignoring bytes the engine never initializes."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


RESERVED2_OFFSET = 34
RESERVED2_BYTES = 6
OBJECT_DOS_BYTES = 12
OBJECT_BASE = 5664
EVENT_OBJECT_BASE = 12864
EVENT_OBJECT_BYTES = 32


def sss_object_count(manifest: Path) -> int:
    data = json.loads(manifest.read_text(errors="replace"))
    for pack in data.get("packs", {}).values():
        for archive in pack.get("archive_summaries", []):
            if archive.get("name") != "SSS":
                continue
            for chunk in archive.get("chunks", []):
                if chunk.get("id") == 2:
                    size = int(chunk["payload_bytes"])
                    if size <= 0 or size % OBJECT_DOS_BYTES != 0:
                        raise ValueError(f"{manifest}: bad SSS object chunk size {size}")
                    return size // OBJECT_DOS_BYTES
    raise ValueError(f"{manifest}: SSS chunk #2 not found")


def canonical_save(data: bytes, object_count: int) -> bytes:
    if len(data) < EVENT_OBJECT_BASE:
        raise ValueError(f"save is too small: {len(data)} bytes")
    if object_count <= 0 or OBJECT_BASE + object_count * OBJECT_DOS_BYTES > EVENT_OBJECT_BASE:
        raise ValueError(f"bad object count: {object_count}")
    if (len(data) - EVENT_OBJECT_BASE) % EVENT_OBJECT_BYTES != 0:
        raise ValueError(f"save event object payload is not record-aligned: {len(data)} bytes")

    out = bytearray(data)
    out[RESERVED2_OFFSET : RESERVED2_OFFSET + RESERVED2_BYTES] = b"\0" * RESERVED2_BYTES
    unused_objects = EVENT_OBJECT_BASE - (OBJECT_BASE + object_count * OBJECT_DOS_BYTES)
    if unused_objects > 0:
        start = OBJECT_BASE + object_count * OBJECT_DOS_BYTES
        out[start:EVENT_OBJECT_BASE] = b"\0" * unused_objects
    return bytes(out)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def diff_blocks(a: bytes, b: bytes) -> list[tuple[int, int]]:
    blocks: list[tuple[int, int]] = []
    start: int | None = None
    for index, (left, right) in enumerate(zip(a, b)):
        if left != right and start is None:
            start = index
        elif left == right and start is not None:
            blocks.append((start, index - start))
            start = None
    if start is not None:
        blocks.append((start, min(len(a), len(b)) - start))
    if len(a) != len(b):
        blocks.append((min(len(a), len(b)), abs(len(a) - len(b))))
    return blocks


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("golden", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()

    object_count = sss_object_count(args.manifest)
    golden = args.golden.read_bytes()
    candidate = args.candidate.read_bytes()
    golden_canon = canonical_save(golden, object_count)
    candidate_canon = canonical_save(candidate, object_count)
    event_count = (len(candidate) - EVENT_OBJECT_BASE) // EVENT_OBJECT_BYTES

    print("# PAL Save Compare")
    print(f"golden: {args.golden}")
    print(f"candidate: {args.candidate}")
    print(f"object_count: {object_count}")
    print(f"event_count: {event_count}")
    print(f"golden_raw_sha256: {sha256(golden)}")
    print(f"candidate_raw_sha256: {sha256(candidate)}")
    print(f"golden_canonical_sha256: {sha256(golden_canon)}")
    print(f"candidate_canonical_sha256: {sha256(candidate_canon)}")
    print(f"raw_diff_blocks: {len(diff_blocks(golden, candidate))}")

    if golden_canon != candidate_canon:
        print("\n## FAIL")
        for start, size in diff_blocks(golden_canon, candidate_canon)[:20]:
            print(f"canonical diff offset={start} bytes={size}")
        return 1

    print("\n## PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
