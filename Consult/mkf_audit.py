#!/usr/bin/env python3
"""Summarize PAL MKF archives for memory-planning work."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def read_chunks(path: Path):
    data = path.read_bytes()
    if len(data) < 4:
        return []

    first_offset = struct.unpack_from("<I", data, 0)[0]
    if first_offset < 4 or first_offset % 4 or first_offset > len(data):
        return []

    count = (first_offset - 4) // 4
    offsets = [struct.unpack_from("<I", data, i * 4)[0] for i in range(count + 1)]
    chunks = []
    for index in range(count):
        start = offsets[index]
        end = offsets[index + 1]
        if start > end or end > len(data):
            continue
        chunk = data[start:end]
        kind = "raw"
        decomp = None
        if len(chunk) >= 8 and chunk[:4] == b"YJ_1":
            kind = "YJ1"
            decomp = struct.unpack_from("<I", chunk, 4)[0]
        chunks.append(
            {
                "index": index,
                "start": start,
                "end": end,
                "size": end - start,
                "kind": kind,
                "decomp": decomp,
            }
        )
    return chunks


def format_chunk(chunk):
    if chunk is None:
        return "none"
    suffix = ""
    if chunk["decomp"] is not None:
        suffix = f", decomp={chunk['decomp']}"
    return f"#{chunk['index']} size={chunk['size']} {chunk['kind']}{suffix}"


def summarize_file(path: Path):
    chunks = read_chunks(path)
    nonempty = [chunk for chunk in chunks if chunk["size"] > 0]
    yj1 = [chunk for chunk in nonempty if chunk["kind"] == "YJ1"]
    max_comp = max(nonempty, key=lambda chunk: chunk["size"], default=None)
    max_decomp = max(yj1, key=lambda chunk: chunk["decomp"] or 0, default=None)

    print(
        f"{path.name:10} file={path.stat().st_size:8} "
        f"chunks={len(chunks):4} nonempty={len(nonempty):4} "
        f"totalChunk={sum(chunk['size'] for chunk in chunks):8} "
        f"maxComp=({format_chunk(max_comp)})",
        end="",
    )
    if yj1:
        print(
            f" yj1={len(yj1):4} "
            f"totalDecomp={sum(chunk['decomp'] or 0 for chunk in yj1):9} "
            f"maxDecomp=({format_chunk(max_decomp)})"
        )
    else:
        print()


def top_chunks(path: Path, count: int):
    chunks = [chunk for chunk in read_chunks(path) if chunk["size"] > 0]
    top_comp = sorted(chunks, key=lambda chunk: chunk["size"], reverse=True)[:count]
    top_decomp = sorted(
        [chunk for chunk in chunks if chunk["decomp"] is not None],
        key=lambda chunk: chunk["decomp"] or 0,
        reverse=True,
    )[:count]

    print(f"\n{path.name} top compressed chunks:")
    for chunk in top_comp:
        print(f"  {format_chunk(chunk)}")

    if top_decomp:
        print(f"{path.name} top decompressed chunks:")
        for chunk in top_decomp:
            print(f"  {format_chunk(chunk)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data_dir", type=Path, help="Directory containing PAL data files")
    parser.add_argument("--top", type=int, default=5, help="Number of top chunks to show")
    args = parser.parse_args()

    data_dir = args.data_dir
    if not data_dir.is_dir():
        raise SystemExit(f"not a directory: {data_dir}")

    print("MKF summary")
    mkfs = sorted(path for path in data_dir.iterdir() if path.suffix.lower() == ".mkf")
    for path in mkfs:
        summarize_file(path)

    for name in ["MGO.MKF", "MAP.MKF", "GOP.MKF", "FIRE.MKF", "F.MKF", "FBP.MKF", "RNG.MKF", "VOC.MKF"]:
        matches = [path for path in mkfs if path.name.lower() == name.lower()]
        if matches:
            top_chunks(matches[0], args.top)


if __name__ == "__main__":
    main()
