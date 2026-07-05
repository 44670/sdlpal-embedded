#!/usr/bin/env python3
"""Build decoded/native PAL resource packs for the embedded runtime.

The generated packs are intentionally simple: every runtime payload is already
raw/native. YJ1 decoding happens here, on the host, never in target code.
"""

from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path


MAGIC = 0x4B504C50
VERSION = 1
HEADER_SIZE = 32
ARCHIVE_ENTRY_SIZE = 12
CHUNK_ENTRY_SIZE = 16

FORMAT_RAW = 0
FORMAT_NATIVE = 1
FORMAT_RNG_FRAMES = 2

ARCHIVE_IDS = {
    "ABC": 1,
    "BALL": 2,
    "DATA": 3,
    "F": 4,
    "FBP": 5,
    "FIRE": 6,
    "GOP": 7,
    "MAP": 8,
    "MGO": 9,
    "MIDI": 10,
    "MUS": 11,
    "PAT": 12,
    "RGM": 13,
    "RNG": 14,
    "SSS": 15,
    "VOC": 16,
}

DEFAULT_NOR = ["ABC", "BALL", "DATA", "F", "FIRE", "MGO", "MIDI", "MUS", "PAT", "RGM", "SSS"]
DEFAULT_TF = ["FBP", "GOP", "MAP", "RNG", "VOC"]


@dataclass(frozen=True)
class Chunk:
    payload: bytes
    fmt: int


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_mkf(path: Path) -> list[bytes]:
    data = path.read_bytes()
    if len(data) < 4:
        raise ValueError(f"not an MKF file: {path}")
    first = u32(data, 0)
    if first < 4 or first % 4 or first > len(data):
        raise ValueError(f"bad MKF offset table: {path}")
    count = (first - 4) // 4
    offsets = [u32(data, i * 4) for i in range(count + 1)]
    chunks = []
    for index in range(count):
        start, end = offsets[index], offsets[index + 1]
        if start > end or end > len(data):
            raise ValueError(f"bad chunk range {path} #{index}: {start}..{end}")
        chunks.append(data[start:end])
    return chunks


def yj1_get_bits(data: bytes, bitptr_ref: list[int], count: int) -> int:
    bitptr = bitptr_ref[0]
    byte_offset = (bitptr >> 4) << 1
    bptr = bitptr & 0xF
    bitptr_ref[0] += count
    word0 = data[byte_offset] | (data[byte_offset + 1] << 8)
    if count > 16 - bptr:
        count = count + bptr - 16
        mask = 0xFFFF >> bptr
        word1 = data[byte_offset + 2] | (data[byte_offset + 3] << 8)
        return ((word0 & mask) << count) | (word1 >> (16 - count))
    return ((word0 << bptr) & 0xFFFF) >> (16 - count)


def yj1_get_loop(data: bytes, bitptr_ref: list[int], header: bytes) -> int:
    if yj1_get_bits(data, bitptr_ref, 1):
        return header[22]
    temp = yj1_get_bits(data, bitptr_ref, 2)
    if temp:
        return yj1_get_bits(data, bitptr_ref, header[18 + temp])
    return header[23]


def yj1_get_count(data: bytes, bitptr_ref: list[int], header: bytes) -> int:
    temp = yj1_get_bits(data, bitptr_ref, 2)
    if temp:
        if yj1_get_bits(data, bitptr_ref, 1):
            return yj1_get_bits(data, bitptr_ref, header[15 + temp])
        return u16(header, 4 + temp * 2)
    return u16(header, 4)


def yj1_decode(data: bytes) -> bytes:
    if len(data) < 16 or data[:4] != b"YJ_1":
        raise ValueError("not a YJ1 chunk")
    out_len = u32(data, 4)
    block_count = u16(data, 12)
    tree_len = data[15] * 2
    flag = data[16 + tree_len :]
    bitptr = [0]
    leaf = [False] * (tree_len + 1)
    value = [0] * (tree_len + 1)
    left = [-1] * (tree_len + 1)
    right = [-1] * (tree_len + 1)
    leaf[0] = False
    left[0] = 1
    right[0] = 2
    for i in range(1, tree_len + 1):
        leaf[i] = not bool(yj1_get_bits(flag, bitptr, 1))
        value[i] = data[15 + i]
        if not leaf[i]:
            left[i] = (value[i] << 1) + 1
            right[i] = left[i] + 1

    src = 16 + tree_len + (((tree_len >> 4) + (1 if tree_len & 0xF else 0)) << 1)
    dest = bytearray()

    for _ in range(block_count):
        header_start = src
        header = data[header_start : header_start + 24]
        src += 4
        comp_len = u16(header, 2)
        if comp_len == 0:
            raw_len = u16(header, 0)
            dest.extend(data[src : src + raw_len])
            src += raw_len
            continue

        src += 20
        block_data = data[src:]
        bitptr = [0]
        while True:
            loop = yj1_get_loop(block_data, bitptr, header)
            if loop == 0:
                break
            for _ in range(loop):
                node = 0
                while not leaf[node]:
                    node = right[node] if yj1_get_bits(block_data, bitptr, 1) else left[node]
                dest.append(value[node])

            loop = yj1_get_loop(block_data, bitptr, header)
            if loop == 0:
                break
            for _ in range(loop):
                count = yj1_get_count(block_data, bitptr, header)
                pos_index = yj1_get_bits(block_data, bitptr, 2)
                pos = yj1_get_bits(block_data, bitptr, header[12 + pos_index])
                for _ in range(count):
                    dest.append(dest[-pos])

        src = header_start + comp_len

    if len(dest) != out_len:
        raise ValueError(f"YJ1 decoded size mismatch: got {len(dest)}, expected {out_len}")
    return bytes(dest)


def decode_if_needed(chunk: bytes) -> bytes:
    if len(chunk) >= 8 and chunk[:4] == b"YJ_1":
        return yj1_decode(chunk)
    return chunk


def decode_rng_movie(chunk: bytes) -> bytes:
    if not chunk:
        return chunk
    if len(chunk) < 4:
        raise ValueError("short RNG movie")
    frame_count = (u32(chunk, 0) - 4) // 4
    offsets = [u32(chunk, i * 4) for i in range(frame_count + 1)]
    frames = []
    for index in range(frame_count):
        frame = chunk[offsets[index] : offsets[index + 1]]
        frames.append(decode_if_needed(frame))

    table_size = 4 + (frame_count + 1) * 4
    out = bytearray()
    out += struct.pack("<I", frame_count)
    cursor = table_size
    for frame in frames:
        out += struct.pack("<I", cursor)
        cursor += len(frame)
    out += struct.pack("<I", cursor)
    for frame in frames:
        out += frame
    return bytes(out)


def load_archive(data_dir: Path, name: str) -> list[Chunk]:
    path = data_dir / f"{name}.MKF"
    if not path.exists():
        path = data_dir / f"{name.lower()}.mkf"
    if not path.exists():
        raise FileNotFoundError(path)

    chunks = []
    for raw in read_mkf(path):
        if name == "RNG":
            chunks.append(Chunk(decode_rng_movie(raw), FORMAT_RNG_FRAMES))
        else:
            chunks.append(Chunk(decode_if_needed(raw), FORMAT_NATIVE))
    return chunks


def align4(value: int) -> int:
    return (value + 3) & ~3


def build_pack(archives: dict[str, list[Chunk]]) -> bytes:
    names = sorted(archives, key=lambda item: ARCHIVE_IDS[item])
    archive_table_offset = HEADER_SIZE
    chunk_table_offset = archive_table_offset + len(names) * ARCHIVE_ENTRY_SIZE
    chunk_table_size = sum(len(archives[name]) * CHUNK_ENTRY_SIZE for name in names)
    data_offset = align4(chunk_table_offset + chunk_table_size)

    archive_entries = bytearray()
    chunk_entries = bytearray()
    payload = bytearray(b"\0" * (data_offset - (chunk_table_offset + chunk_table_size)))
    cursor = data_offset

    for name in names:
        chunks = archives[name]
        archive_entries += struct.pack("<HHII", ARCHIVE_IDS[name], len(chunks), chunk_table_offset + len(chunk_entries), 0)
        for chunk in chunks:
            if cursor != align4(cursor):
                pad = align4(cursor) - cursor
                payload += b"\0" * pad
                cursor += pad
            chunk_entries += struct.pack("<IIHHI", cursor, len(chunk.payload), chunk.fmt, 0, 0)
            payload += chunk.payload
            cursor += len(chunk.payload)

    header = struct.pack(
        "<IHHHHIIIII",
        MAGIC,
        VERSION,
        HEADER_SIZE,
        len(names),
        0,
        archive_table_offset,
        data_offset,
        0,
        cursor,
        0,
    )
    return header + archive_entries + chunk_entries + payload


def checked_range(offset: int, size: int, total: int) -> bool:
    return 0 <= offset <= total and 0 <= size <= total - offset


def verify_pack(pack: bytes) -> None:
    if len(pack) < HEADER_SIZE:
        raise ValueError("short pack header")
    magic, version, header_size, archive_count, _reserved0 = struct.unpack_from("<IHHHH", pack, 0)
    archive_table_offset = u32(pack, 12)
    pack_size = u32(pack, 24)

    if magic != MAGIC:
        raise ValueError("bad pack magic")
    if version != VERSION:
        raise ValueError(f"bad pack version: {version}")
    if header_size != HEADER_SIZE:
        raise ValueError(f"bad pack header size: {header_size}")
    if pack_size != len(pack):
        raise ValueError(f"pack size field is {pack_size}, actual {len(pack)}")
    if not checked_range(archive_table_offset, archive_count * ARCHIVE_ENTRY_SIZE, len(pack)):
        raise ValueError("archive table out of range")

    seen_archives: set[int] = set()
    for archive_index in range(archive_count):
        archive_entry_offset = archive_table_offset + archive_index * ARCHIVE_ENTRY_SIZE
        archive_id, chunk_count = struct.unpack_from("<HH", pack, archive_entry_offset)
        chunk_table_offset = u32(pack, archive_entry_offset + 4)
        if archive_id in seen_archives:
            raise ValueError(f"duplicate archive id: {archive_id}")
        seen_archives.add(archive_id)
        if not checked_range(chunk_table_offset, chunk_count * CHUNK_ENTRY_SIZE, len(pack)):
            raise ValueError(f"chunk table out of range for archive {archive_id}")

        for chunk_index in range(chunk_count):
            chunk_entry_offset = chunk_table_offset + chunk_index * CHUNK_ENTRY_SIZE
            payload_offset = u32(pack, chunk_entry_offset)
            payload_size = u32(pack, chunk_entry_offset + 4)
            flags = u16(pack, chunk_entry_offset + 10)
            if flags != 0:
                raise ValueError(f"runtime chunk has flags 0x{flags:04x}: archive {archive_id} chunk {chunk_index}")
            if not checked_range(payload_offset, payload_size, len(pack)):
                raise ValueError(f"payload out of range: archive {archive_id} chunk {chunk_index}")


def parse_names(raw: str | None, default: list[str]) -> list[str]:
    if raw is None:
        return default
    names = [item.strip().upper() for item in raw.split(",") if item.strip()]
    unknown = [name for name in names if name not in ARCHIVE_IDS]
    if unknown:
        raise SystemExit(f"unknown archive names: {', '.join(unknown)}")
    return names


def write_pack(data_dir: Path, out_path: Path, names: list[str]) -> None:
    archives = {name: load_archive(data_dir, name) for name in names}
    pack = build_pack(archives)
    verify_pack(pack)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(pack)
    print(f"{out_path}: {len(pack)} bytes, archives={','.join(names)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data_dir", type=Path)
    parser.add_argument("--out-nor", type=Path, required=True)
    parser.add_argument("--out-tf", type=Path, required=True)
    parser.add_argument("--nor", help="comma-separated archives for NOR pack")
    parser.add_argument("--tf", help="comma-separated archives for TF pack")
    args = parser.parse_args()

    data_dir = args.data_dir
    nor_names = parse_names(args.nor, DEFAULT_NOR)
    tf_names = parse_names(args.tf, DEFAULT_TF)
    overlap = sorted(set(nor_names) & set(tf_names))
    if overlap:
        raise SystemExit(f"archives listed in both packs: {', '.join(overlap)}")

    write_pack(data_dir, args.out_nor, nor_names)
    write_pack(data_dir, args.out_tf, tf_names)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
